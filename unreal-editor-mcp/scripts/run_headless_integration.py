#!/usr/bin/env python3
"""Run the disposable-editor cross-process integration scenarios."""

from __future__ import annotations

import sys
from pathlib import Path


SCRIPTS_ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS_ROOT))

from headless_integration.lifecycle import (  # noqa: E402,F401
    configure_editor_environment,
    main,
    resolve_editor_executable,
)


if __name__ == "__main__":
    raise SystemExit(main())
