---
feature_id: argument-errors
status: completed
depends_on: []
released_in: "0.61.0"
---
# Detailed tool argument validation errors

**Depends on:** None.

## Contract

Improve Python MCP schema rejection diagnostics without changing accepted arguments, tool schemas, or dispatch. Report the first field failure for up to four closest request shapes, preferring supplied constant discriminators, and distinguish no matches from ambiguous multiple matches. Preserve nested field/array paths and expected types, patterns, and numeric or collection bounds. Cap schema errors at 4,096 UTF-8 bytes and retained branch reasons at 768 bytes; report omitted shapes and never echo supplied field values.

The owning component is the [Python MCP server](../../architecture/python-mcp-server.md), with the error boundary defined in [Python wire contracts](../../types/python/contracts.md#python-wire-contracts). Validation happens in the stdio process before HTTP or editor/Game-thread dispatch and creates no persistent state. The base version is 0.61.0; companion API and companion versions are unchanged.

## Verification

- [x] Focused invalid, ambiguous, nested-reference, discriminator, limit, Unicode, and stdio regressions, including delivery beyond the normal 512-character protocol-error cap.
- [x] Full affected Python suite on Windows: 216 tests, one symlink-permission skip.
- [x] Windows adaptive and forced-unity (`-ForceUnity -DisableAdaptiveUnity`) editor builds for synchronized native version metadata.
- [x] Isolated Win64 base-plugin packaging.
- [x] Complete production-bridge restart workflow and all 71 expected native Automation cases for the rebuilt base version.
- [x] Documentation lint and final release/history metadata.

Python validation owns the behavior change. Native compilation/packaging verifies the synchronized version header and descriptor; existing native command handlers and bridge behavior are unchanged.

Windows checks passed on 2026-10-09. Preferred macOS Python/stdio and synchronized-version build/packaging verification remains in the [native platform test backlog](../../../ROADMAP.md#native-platform-test-backlog).
