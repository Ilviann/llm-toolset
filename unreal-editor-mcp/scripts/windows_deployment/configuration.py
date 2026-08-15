"""Pure launch-definition and host-configuration previews."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Mapping

try:
    from scripts.unreal_tooling.paths import resolved
    from scripts.unreal_tooling.plugins import APPLICATION_ROOT
except ModuleNotFoundError:
    from unreal_tooling.paths import resolved  # type: ignore[no-redef]
    from unreal_tooling.plugins import APPLICATION_ROOT  # type: ignore[no-redef]

from .discovery import DeploymentError, validate_editor_lifecycle_executable
from .models import ProjectInfo


SERVER_ENTRY = APPLICATION_ROOT / "server.py"
SERVER_NAME = "unreal-editor"


def mcp_server_definition(
    project: ProjectInfo,
    python_executable: Path | None = None,
    *,
    writable: bool = False,
    editor_lifecycle: Path | None = None,
) -> dict[str, object]:
    if type(writable) is not bool:
        raise DeploymentError("writable must be Boolean")
    executable = resolved(Path(sys.executable) if python_executable is None else python_executable)
    arguments = [str(SERVER_ENTRY), str(project.descriptor)]
    if writable:
        arguments.append("--writable")
    if editor_lifecycle is not None:
        arguments.extend(("--editor-lifecycle", str(validate_editor_lifecycle_executable(editor_lifecycle))))
    return {"command": str(executable), "args": arguments}


def format_lm_studio_json(definition: Mapping[str, object]) -> str:
    return json.dumps({"mcpServers": {SERVER_NAME: dict(definition)}}, indent=2, ensure_ascii=False)


def format_codex_toml(definition: Mapping[str, object]) -> str:
    command = definition.get("command")
    arguments = definition.get("args")
    if not isinstance(command, str) or not isinstance(arguments, list) or not all(
        isinstance(argument, str) for argument in arguments
    ):
        raise ValueError("MCP server definition must contain a string command and string args")
    encoded_arguments = ",\n".join(
        f"  {json.dumps(argument, ensure_ascii=False)}" for argument in arguments
    )
    return (
        f"[mcp_servers.{SERVER_NAME}]\n"
        f"command = {json.dumps(command, ensure_ascii=False)}\n"
        "args = [\n"
        f"{encoded_arguments}\n"
        "]"
    )


def format_mcp_settings_preview(definition: Mapping[str, object]) -> str:
    return (
        "mcp.json (LM Studio)\n"
        "====================\n\n"
        f"{format_lm_studio_json(definition)}\n\n"
        "config.toml (ChatGPT Codex)\n"
        "============================\n\n"
        f"{format_codex_toml(definition)}"
    )


def lm_studio_json(
    project: ProjectInfo,
    python_executable: Path | None = None,
    *,
    writable: bool = False,
    editor_lifecycle: Path | None = None,
) -> str:
    return format_lm_studio_json(mcp_server_definition(
        project,
        python_executable,
        writable=writable,
        editor_lifecycle=editor_lifecycle,
    ))
