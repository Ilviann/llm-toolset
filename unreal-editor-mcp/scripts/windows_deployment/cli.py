"""Windows-only graphical deployment entrypoint."""

from __future__ import annotations

import platform
import sys

from .controller import DeploymentWindow


def main() -> int:
    if platform.system() != "Windows":
        print("This graphical deployment helper is supported only on Windows.", file=sys.stderr)
        return 1
    try:
        DeploymentWindow().run()
    except ImportError as error:
        print(f"Tkinter is required for the graphical deployment helper: {error}", file=sys.stderr)
        return 1
    return 0
