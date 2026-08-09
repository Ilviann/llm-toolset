#!/usr/bin/env python3
"""Stable compatibility entrypoint for Windows Unreal plugin deployment."""

from __future__ import annotations

import subprocess

try:
    from scripts import packaging as package_plugin
    from scripts.windows_deployment import *  # type: ignore[F403]  # noqa: F401,F403
except ModuleNotFoundError:
    import packaging as package_plugin  # type: ignore[no-redef]
    from windows_deployment import *  # type: ignore[F403]  # noqa: F401,F403


if __name__ == "__main__":
    raise SystemExit(main())
