"""Tool execution, dependency routing, and project-path preflight checks."""

from __future__ import annotations

from typing import Any, Protocol

from . import __version__
from .assets import AssetError
from .bridge import BridgeError
from .launcher import LauncherError
from .tool_catalog import MODE_TOOL_NAMES, Mode
from .waiting import DEFAULT_STARTUP_WINDOW_MS, OperationWaiter


class BridgeClient(Protocol):
    def call(self, command: str, arguments: dict[str, Any] | None = None) -> Any: ...


class AssetManager(Protocol):
    def import_asset(self, source: Any, destination: Any) -> dict[str, Any]: ...
    def create_folder(self, path: Any) -> dict[str, Any]: ...
    def validate_folder(self, path: Any) -> None: ...
    def validate_file(self, path: Any, extensions: set[str] | None = None) -> None: ...
    def validate_new_file(self, path: Any, extensions: set[str]) -> None: ...


class EditorStarter(Protocol):
    @property
    def configured(self) -> bool: ...

    def start(self, bridge: BridgeClient) -> dict[str, str]: ...


TOOL_ERRORS = (AssetError, BridgeError, LauncherError, TypeError, ValueError)

BRIDGE_COMMANDS = {
    "capabilities": "capabilities",
    "editor_state": "state",
    "get_diagnostics": "diagnostics",
    "list_assets": "assets",
    "asset_info": "asset_info",
    "scan_asset": "scan_asset",
    "create_resource": "create_resource",
    "create_scene": "create_scene",
    "open_scene": "open_scene",
    "scene_tree": "tree",
    "node_info": "inspect",
    "add_node": "add_node",
    "instantiate_scene": "instantiate_scene",
    "set_property": "set_property",
    "select_node": "select",
    "scene_control": "control",
    "project_settings_get": "project_settings_get",
    "project_settings_patch": "project_settings_patch",
    "input_map_patch": "input_map_patch",
}


class ToolDispatcher:
    """Execute tools exposed by one mode against injected local services."""

    def __init__(
        self,
        bridge: BridgeClient,
        assets: AssetManager | None,
        *,
        mode: Mode,
        launcher: EditorStarter | None,
    ) -> None:
        self.bridge = bridge
        self.assets = assets
        self.mode = mode
        self.launcher = launcher
        self.tool_names = MODE_TOOL_NAMES[mode]
        self.waiter = OperationWaiter(bridge)

    def call(self, name: str, arguments: dict[str, Any]) -> Any:
        if name == "start_editor":
            return self._start_editor(arguments)
        if name == "import_asset":
            return self._import_asset(arguments)
        if name == "create_folder":
            return self._create_folder(arguments)

        command = BRIDGE_COMMANDS.get(name)
        if command is None:
            raise ValueError("Unknown tool")
        self._validate_project_path(name, arguments)
        wait, timeout_ms = self.waiter.options(arguments) if name in {
            "open_scene", "scan_asset", "scene_control"
        } else (False, 0)
        bridge_arguments = (
            self.waiter.bridge_arguments(arguments) if name in {
                "open_scene", "scan_asset", "scene_control"
            } else arguments
        )
        output = self.bridge.call(command, bridge_arguments)
        if wait and isinstance(output, dict):
            operation_id = output.get("operation_id")
            if name == "open_scene":
                output["wait"] = self.waiter.wait_for_scene(
                    str(arguments.get("path", "")), operation_id, timeout_ms
                )
            elif name == "scan_asset":
                output["wait"] = self.waiter.wait_for_asset(
                    str(arguments.get("path", "")), operation_id, timeout_ms
                )
            elif name == "scene_control":
                output["wait"] = self._wait_for_control(
                    arguments, output, operation_id, timeout_ms
                )
        return self._augment_capabilities(output) if name == "capabilities" else output

    def _start_editor(self, arguments: dict[str, Any]) -> dict[str, str]:
        if arguments:
            raise ValueError("start_editor does not accept arguments")
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

    def _validate_project_path(self, name: str, arguments: dict[str, Any]) -> None:
        if self.assets is None:
            return
        if name == "list_assets":
            self.assets.validate_folder(arguments.get("folder", "."))
        elif name in {"asset_info", "scan_asset"}:
            self.assets.validate_file(arguments.get("path"))
        elif name == "create_scene":
            self.assets.validate_new_file(arguments.get("path"), {".tscn"})
        elif name == "create_resource":
            self.assets.validate_new_file(arguments.get("path"), {".tres"})
        elif name == "open_scene":
            self.assets.validate_file(arguments.get("path"), {".tscn", ".scn"})
        elif name == "instantiate_scene":
            self.assets.validate_file(arguments.get("scene"), {".tscn", ".scn"})

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
            raise ValueError("Godot editor did not return a run ID")
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
