"""Command-line composition root."""

from __future__ import annotations

import argparse

from .bridge import UnrealBridge
from .errors import DomainError
from .lifecycle import EditorLifecycle, resolve_editor_executable
from .platforms import DEFAULT_PLATFORM
from .project import ProjectLayout
from .server import MCPServer
from .stdio import configure_standard_streams, serve


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Blueprint-family MCP bridge for Unreal Editor 5.8+",
        allow_abbrev=False,
    )
    parser.add_argument("project", help="Unreal .uproject file or its containing folder")
    parser.add_argument("--port", type=int, help="Require this active bridge port")
    parser.add_argument("--timeout", type=float, default=3.0, help="HTTP timeout in seconds (default: 3)")
    parser.add_argument(
        "--writable",
        action="store_true",
        help="Publish project-content mutation tools (default: readonly)",
    )
    parser.add_argument(
        "--editor-lifecycle",
        metavar="ABSOLUTE_UNREALEDITOR_EXECUTABLE",
        help="Publish lifecycle control using this absolute UnrealEditor executable",
    )
    parser.add_argument(
        "--lifecycle-timeout",
        type=float,
        default=120.0,
        help="Launch/shutdown/restart timeout in seconds (default: 120)",
    )
    return parser


def main() -> None:
    configure_standard_streams()
    parser = build_parser()
    args = parser.parse_args()
    try:
        layout = ProjectLayout.resolve(args.project)
        editor = (
            resolve_editor_executable(args.editor_lifecycle, DEFAULT_PLATFORM)
            if args.editor_lifecycle is not None
            else None
        )
        bridge = UnrealBridge(layout, port=args.port, timeout=args.timeout)
        lifecycle = None
        if editor is not None:
            lifecycle = EditorLifecycle(
                layout,
                bridge,
                editor_executable=editor,
                startup_timeout=args.lifecycle_timeout,
            )
        serve(MCPServer(
            bridge,
            project_identity=layout.identity(),
            lifecycle=lifecycle,
            writable=args.writable,
        ))
    except DomainError as exc:
        parser.error(str(exc))
