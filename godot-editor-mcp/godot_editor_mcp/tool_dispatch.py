"""Tool execution, dependency routing, and project-path preflight checks."""

from __future__ import annotations

import os
import stat
from pathlib import Path
from typing import Any, Protocol

from . import __version__
from .errors import (
    AssetError,
    BridgeError,
    InvalidArgumentError,
    InvalidResponseError,
    LauncherError,
)
from .tool_catalog import BRIDGE_COMMANDS, MODE_TOOL_NAMES, SPEC_BY_NAME, Mode, ToolSpec
from .stdio import ToolImageResult
from .waiting import DEFAULT_STARTUP_WINDOW_MS


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
MAX_CAPTURE_BYTES = 8 * 1024 * 1024
MAX_CAPTURE_OUTPUT_DIMENSION = 2048
MAX_CAPTURE_OUTPUT_PIXELS = 2048 * 2048


class BridgeClient(Protocol):
    def call(self, command: str, arguments: dict[str, Any] | None = None) -> Any: ...


class AssetManager(Protocol):
    @property
    def project(self) -> Path: ...

    def import_asset(self, source: Any, destination: Any) -> dict[str, Any]: ...
    def create_folder(self, path: Any) -> dict[str, Any]: ...
    def validate_folder(self, path: Any) -> None: ...
    def validate_file(self, path: Any, extensions: set[str] | None = None) -> None: ...
    def validate_new_file(self, path: Any, extensions: set[str]) -> None: ...


class EditorStarter(Protocol):
    @property
    def configured(self) -> bool: ...

    def start(self, bridge: BridgeClient) -> dict[str, str]: ...


class Waiter(Protocol):
    def cancel(self) -> None: ...
    def options(self, arguments: dict[str, Any]) -> tuple[bool, int]: ...
    def bridge_arguments(self, arguments: dict[str, Any]) -> dict[str, Any]: ...
    def wait_for_scene(self, path: str, operation_id: Any, timeout_ms: int) -> dict[str, Any]: ...
    def wait_for_asset(self, path: str, operation_id: Any, timeout_ms: int) -> dict[str, Any]: ...
    def wait_for_run(
        self, run_id: int, operation_id: Any, timeout_ms: int, startup_window_ms: int
    ) -> dict[str, Any]: ...
    def wait_for_stop(self, run_id: int, operation_id: Any, timeout_ms: int) -> dict[str, Any]: ...
    def wait_for_reload(
        self,
        operation_id: Any,
        expected_project_hash: Any,
        expected_bridge_version: Any,
        timeout_ms: int,
    ) -> dict[str, Any]: ...


class ToolDispatcher:
    """Execute tools exposed by one mode against injected local services."""

    def __init__(
        self,
        bridge: BridgeClient,
        assets: AssetManager | None,
        *,
        mode: Mode,
        launcher: EditorStarter | None,
        waiter: Waiter,
    ) -> None:
        self.bridge = bridge
        self.assets = assets
        self.mode = mode
        self.launcher = launcher
        self.tool_names = MODE_TOOL_NAMES[mode]
        self.waiter = waiter
        self._local_handlers = {
            "start_editor": self._start_editor,
            "import_asset": self._import_asset,
            "create_folder": self._create_folder,
        }

    def call(self, name: str, arguments: dict[str, Any]) -> Any:
        spec = SPEC_BY_NAME.get(name)
        if spec is None:
            raise RuntimeError("Unknown tool reached dispatcher")
        if spec.target != "bridge":
            handler = self._local_handlers.get(spec.local_handler or "")
            if handler is None:
                raise RuntimeError("Tool has no local execution handler")
            return handler(arguments)
        return self._call_bridge(spec, arguments)

    def _call_bridge(self, spec: ToolSpec, arguments: dict[str, Any]) -> Any:
        if spec.bridge_command is None:
            raise RuntimeError("Tool has no bridge command")
        self._validate_project_path(spec, arguments)
        wait, timeout_ms = (
            self.waiter.options(arguments)
            if spec.wait_strategy != "none"
            else (False, 0)
        )
        bridge_arguments = (
            self.waiter.bridge_arguments(arguments)
            if spec.wait_strategy != "none"
            else arguments
        )
        output = self.bridge.call(spec.bridge_command, bridge_arguments)
        if spec.name == "capture_game_view":
            return self._consume_capture(output)
        if wait and isinstance(output, dict):
            operation_id = output.get("operation_id")
            if spec.wait_strategy == "scene":
                output["wait"] = self.waiter.wait_for_scene(
                    str(arguments.get("path", "")), operation_id, timeout_ms
                )
            elif spec.wait_strategy == "asset":
                output["wait"] = self.waiter.wait_for_asset(
                    str(arguments.get("path", "")), operation_id, timeout_ms
                )
            elif spec.wait_strategy == "control":
                output["wait"] = self._wait_for_control(
                    arguments, output, operation_id, timeout_ms
                )
            elif spec.wait_strategy == "reload":
                output["wait"] = self.waiter.wait_for_reload(
                    operation_id,
                    output.get("project_hash"),
                    output.get("bridge_version"),
                    timeout_ms,
                )
        return (
            self._augment_capabilities(output)
            if spec.name == "capabilities"
            else output
        )

    def _consume_capture(self, output: Any) -> ToolImageResult:
        if not isinstance(output, dict):
            raise InvalidResponseError("Godot runtime returned invalid capture metadata")
        capture_id = output.get("capture_id")
        width = output.get("width")
        height = output.get("height")
        encoded_bytes = output.get("bytes")
        if (
            not isinstance(capture_id, str)
            or len(capture_id) != 32
            or any(character not in "0123456789abcdef" for character in capture_id)
        ):
            raise InvalidResponseError("Godot runtime returned an invalid capture ID")
        if (
            type(width) is not int
            or type(height) is not int
            or width < 1
            or height < 1
            or width > MAX_CAPTURE_OUTPUT_DIMENSION
            or height > MAX_CAPTURE_OUTPUT_DIMENSION
            or width * height > MAX_CAPTURE_OUTPUT_PIXELS
            or type(encoded_bytes) is not int
            or encoded_bytes < len(PNG_SIGNATURE)
            or encoded_bytes > MAX_CAPTURE_BYTES
        ):
            raise InvalidResponseError("Godot runtime returned invalid capture bounds")
        assets = self._require_assets("Game-view capture is unavailable")
        project = Path(assets.project)
        capture_folder = project / ".godot" / "godot_mcp" / "captures"
        capture_path = capture_folder / f"{capture_id}.png"
        safe_folder = False
        try:
            resolved_folder = capture_folder.resolve(strict=True)
            expected_folder = (
                project.resolve(strict=True) / ".godot" / "godot_mcp" / "captures"
            )
            if resolved_folder != expected_folder:
                raise InvalidResponseError("Godot runtime capture path is unsafe")
            safe_folder = True
            flags = os.O_RDONLY | getattr(os, "O_BINARY", 0)
            flags |= getattr(os, "O_NOFOLLOW", 0)
            descriptor = os.open(capture_path, flags)
            try:
                file_stat = os.fstat(descriptor)
                if (
                    not stat.S_ISREG(file_stat.st_mode)
                    or file_stat.st_size != encoded_bytes
                ):
                    raise InvalidResponseError("Godot runtime capture file is invalid")
                if file_stat.st_size > MAX_CAPTURE_BYTES:
                    raise InvalidResponseError("Godot runtime capture is too large")
                chunks: list[bytes] = []
                remaining = MAX_CAPTURE_BYTES + 1
                while remaining > 0:
                    chunk = os.read(descriptor, min(1024 * 1024, remaining))
                    if not chunk:
                        break
                    chunks.append(chunk)
                    remaining -= len(chunk)
                data = b"".join(chunks)
            finally:
                os.close(descriptor)
            if len(data) != encoded_bytes:
                raise InvalidResponseError("Godot runtime capture file is invalid")
            if data[:8] != PNG_SIGNATURE:
                raise InvalidResponseError("Godot runtime capture is not a valid PNG")
            if len(data) < 24 or data[12:16] != b"IHDR":
                raise InvalidResponseError("Godot runtime capture has no PNG header")
            png_width = int.from_bytes(data[16:20], "big")
            png_height = int.from_bytes(data[20:24], "big")
            if (png_width, png_height) != (width, height):
                raise InvalidResponseError("Godot runtime capture dimensions do not match")
            metadata = {
                key: value for key, value in output.items()
                if key in {
                    "capture_id", "run_id", "width", "height",
                    "source_width", "source_height", "bytes", "format",
                }
            }
            return ToolImageResult(data=data, metadata=metadata)
        except InvalidResponseError:
            raise
        except (OSError, RuntimeError, ValueError):
            raise InvalidResponseError("Godot runtime capture file is unavailable") from None
        finally:
            try:
                if safe_folder:
                    os.unlink(capture_path)
            except OSError:
                pass

    def close(self) -> None:
        self.waiter.cancel()

    def _start_editor(self, arguments: dict[str, Any]) -> dict[str, str]:
        if arguments:
            raise InvalidArgumentError("start_editor does not accept arguments")
        if self.launcher is None:
            raise LauncherError("Godot executable is not configured; set GODOT_EXECUTABLE")
        return self.launcher.start(self.bridge)

    def _import_asset(self, arguments: dict[str, Any]) -> dict[str, Any]:
        wait, timeout_ms = self.waiter.options(arguments)
        assets = self._require_assets("Asset import is unavailable")
        output = assets.import_asset(arguments.get("source"), arguments.get("destination"))
        self._queue_scan(output, output["destination"])
        if wait:
            output["wait"] = self.waiter.wait_for_asset(
                output["destination"], output.get("operation_id"), timeout_ms
            )
        return output

    def _create_folder(self, arguments: dict[str, Any]) -> dict[str, Any]:
        assets = self._require_assets("Folder creation is unavailable")
        output = assets.create_folder(arguments.get("path"))
        self._queue_scan(output, output["path"])
        return output

    def _require_assets(self, message: str) -> AssetManager:
        if self.assets is None:
            raise AssetError(message)
        return self.assets

    def _validate_project_path(
        self, spec: ToolSpec, arguments: dict[str, Any]
    ) -> None:
        if self.assets is None or spec.path_kind == "none" or spec.path_field is None:
            return
        value = arguments.get(spec.path_field, "." if spec.path_kind == "folder" else None)
        extensions = set(spec.path_extensions) or None
        if spec.path_kind == "folder":
            self.assets.validate_folder(value)
        elif spec.path_kind == "file":
            self.assets.validate_file(value, extensions)
        elif spec.path_kind == "new_file":
            assert extensions is not None
            self.assets.validate_new_file(value, extensions)

    def _augment_capabilities(self, output: Any) -> Any:
        if not isinstance(output, dict):
            return output
        capabilities = {
            **output,
            "mcp_server_version": __version__,
            "mode": self.mode,
            "tools": list(self.tool_names),
        }
        if self.mode == "large":
            capabilities["editor_launcher"] = {
                "configured": bool(self.launcher is not None and self.launcher.configured)
            }
        return capabilities

    def _queue_scan(self, output: dict[str, Any], path: str) -> None:
        try:
            scan = self.bridge.call("scan_asset", {"path": path.removeprefix("res://")})
            if isinstance(scan, dict):
                output["scan"] = scan.get("scan", "queued")
                output["operation_id"] = scan.get("operation_id")
            else:
                output["scan"] = "queued"
        except BridgeError as exc:
            output["scan"] = "pending"
            output["warning"] = str(exc)

    def _wait_for_control(
        self,
        arguments: dict[str, Any],
        output: dict[str, Any],
        operation_id: Any,
        timeout_ms: int,
    ) -> dict[str, Any]:
        action = arguments.get("action")
        run_id = output.get("run_id", arguments.get("run_id"))
        if type(run_id) is not int:
            raise InvalidResponseError("Godot editor did not return a run ID")
        if action == "run":
            return self.waiter.wait_for_run(
                run_id,
                operation_id,
                timeout_ms,
                arguments.get("startup_window_ms", DEFAULT_STARTUP_WINDOW_MS),
            )
        if action == "stop":
            return self.waiter.wait_for_stop(run_id, operation_id, timeout_ms)
        return {"completed": True}
