"""Command-line composition root for the Godot editor MCP server."""

from __future__ import annotations

import argparse
import os

from .assets import AssetError, ProjectAssets
from .bridge import BridgeError, GodotBridge
from .launcher import EditorLauncher
from .server import MCPServer
from .stdio import serve
from .tool_catalog import MODES, Mode
from .tool_dispatch import AssetManager, BridgeClient, EditorStarter


def run(
    bridge: BridgeClient,
    assets: AssetManager,
    *,
    mode: Mode = "tiny",
    launcher: EditorStarter | None = None,
) -> None:
    serve(MCPServer(bridge, assets, mode=mode, launcher=launcher))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Small MCP bridge for the Godot 4 editor")
    parser.add_argument("project", help="Godot project folder")
    parser.add_argument(
        "--mode",
        choices=MODES,
        default="tiny",
        help="Available toolset: tiny (default), small, or large",
    )
    parser.add_argument("--port", type=int, default=6505, help="Plugin port (default: 6505)")
    parser.add_argument("--import-root", help="Folder containing assets staged for import")
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    try:
        bridge = GodotBridge(args.project, port=args.port)
        launcher = EditorLauncher(args.project, os.environ.get("GODOT_EXECUTABLE"))
        run(
            bridge,
            ProjectAssets(args.project, args.import_root),
            mode=args.mode,
            launcher=launcher,
        )
    except (AssetError, BridgeError) as exc:
        parser.error(str(exc))
