# Automated verification

## Purpose

Protect configuration precedence/confinement, filesystem security, text/atomic editing, MCP contracts, permission filtering, and real stdio startup with fast offline standard-library tests.

## Owned source

- `tests/test_configuration.py` — configuration schema, precedence, paths, case/allowlist behavior, and immutable settings.
- `tests/test_filesystem.py` — path/hidden/symlink/permission policy, text classification/ranges, format preservation, and atomic safeguards.
- `tests/test_server.py` — MCP catalog/dispatch/errors plus subprocess stdio and startup behavior.

## Test layers

1. Pure configuration and filesystem units with temporary roots and injected platform/stat behavior.
2. In-process MCP request/dispatch tests.
3. Real subprocess startup and newline-delimited stdio tests through the top-level launcher.

## Invariants

- Tests derive release/behavior expectations from source, metadata, and runtime results—not prose documentation.
- Every platform branch is exercised even when native validation is pending.
- Security coverage includes traversal, requested/resolved symlinks, hidden/protected names, Windows attributes, permissions, binary/UTF-8/size limits, write revalidation, mode preservation, and cleanup.
- Protocol coverage includes initialization/version, permission-filtered schemas, tool errors, notifications, parse/internal errors, and stdout/stderr isolation.

## Known pressure

If the MCP component is later split, move protocol units and subprocess startup cases into separate test files with it. Do not create test-only churn independently.

## Change and verification guide

Run focused cases while iterating, then `python3 -m unittest discover -s tests -v` after behavior changes. Native Windows symlink tests may require Developer Mode or privilege; record skips and native validation honestly.
