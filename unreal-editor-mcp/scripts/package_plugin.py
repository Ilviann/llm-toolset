#!/usr/bin/env python3
"""Stable compatibility entrypoint for Unreal plugin packaging."""

from __future__ import annotations

import platform

try:
    from scripts.packaging import *  # type: ignore[F403]  # noqa: F401,F403
except ModuleNotFoundError:
    from packaging import *  # type: ignore[F403]  # noqa: F401,F403


if __name__ == "__main__":
    raise SystemExit(main())
