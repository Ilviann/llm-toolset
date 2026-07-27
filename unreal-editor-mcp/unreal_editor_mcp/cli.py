"""Command-line composition root."""

from __future__ import annotations

import argparse

from .bridge import UnrealBridge
from .errors import DomainError
from .lifecycle import EditorLifecycle, resolve_editor_executable
from .platforms import DEFAULT_PLATFORM
from .project import ProjectLayout
from .server import MCPServer
from .stdio import serve


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Blueprint-family MCP bridge for Unreal Editor 5.8+")
    parser.add_argument("project", help="Unreal .uproject file or its containing folder")
    parser.add_argument("--port", type=int, help="Require this active bridge port")
    parser.add_argument("--timeout", type=float, default=3.0, help="HTTP timeout in seconds (default: 3)")
    parser.add_argument(
        "--tool-mode",
        choices=("default", "large"),
        default="default",
        help="Tool surface; large adds opt-in configured editor lifecycle control",
    )
    parser.add_argument("--editor", help="Absolute UnrealEditor executable configured for large-mode launch/restart")
    parser.add_argument(
        "--lifecycle-timeout",
        type=float,
        default=120.0,
        help="Launch/shutdown/restart timeout in seconds (default: 120)",
    )
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    try:
        layout = ProjectLayout.resolve(args.project)
        if args.editor and args.tool_mode != "large":
            parser.error("--editor requires --tool-mode large")
        bridge = UnrealBridge(layout, port=args.port, timeout=args.timeout)
        lifecycle = None
        if args.tool_mode == "large":
            editor = resolve_editor_executable(args.editor, DEFAULT_PLATFORM) if args.editor else None
            lifecycle = EditorLifecycle(
                layout,
                bridge,
                editor_executable=editor,
                startup_timeout=args.lifecycle_timeout,
            )
        serve(MCPServer(bridge, lifecycle=lifecycle, tool_mode=args.tool_mode))
    except DomainError as exc:
        parser.error(str(exc))
