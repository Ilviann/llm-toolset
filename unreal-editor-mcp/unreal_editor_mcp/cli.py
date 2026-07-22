"""Command-line composition root."""

from __future__ import annotations

import argparse

from .bridge import UnrealBridge
from .errors import DomainError
from .project import ProjectLayout
from .server import MCPServer
from .stdio import serve


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Blueprint-family MCP bridge for Unreal Editor 5.8+")
    parser.add_argument("project", help="Unreal .uproject file or its containing folder")
    parser.add_argument("--port", type=int, help="Require this active bridge port")
    parser.add_argument("--timeout", type=float, default=3.0, help="HTTP timeout in seconds (default: 3)")
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    try:
        layout = ProjectLayout.resolve(args.project)
        serve(MCPServer(UnrealBridge(layout, port=args.port, timeout=args.timeout)))
    except DomainError as exc:
        parser.error(str(exc))
