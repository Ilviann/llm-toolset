# Automated verification

## Purpose

Protect behavior, security boundaries, release contracts, platform branches, Godot service boundaries, and cross-process integration without adding runtime dependencies.

## Owned source

- `tests/*.py` — Python units, MCP/stdio integration, schemas, contracts, assets, bridge/discovery, payloads/waits, launcher, and optional live reload integration.
- `plugin/tests/*.gd` — focused headless Godot checks for diagnostics, records, routing, infrastructure, trackers, cursors, service boundaries, runtime validation, transactions, workflows, and authenticated bridge startup.

## Test layers

1. Fast Python `unittest` suite with fakes for platform and collaborator branches.
2. Focused headless GDScript scripts against the minimal `plugin` project.
3. Plugin parser/load smoke check.
4. Opt-in subprocess integration using a real Godot editor, live bridge, runtime debugger, persistence, and reload/reconnect.

## Invariants

- Tests derive expected behavior and release consistency from source, executable metadata, runtime contracts, and behavioral results—not prose documentation.
- Security boundaries cover invalid input, traversal/symlinks, resource limits, authentication failure, client deadlines, stale identities, and rollback.
- Every platform-specific branch is tested; native validation status is documented separately.
- Cross-language command, error, limit, version, and capability drift fails before release.

## Change and verification guide

Choose tests from the owning architecture page, run focused checks during implementation, then run the complete Python suite after behavior changes. Run every affected GDScript phase and the plugin load check for editor-side changes. Use `GODOT_RELOAD_INTEGRATION=1` for cross-process contracts. A dummy-renderer `Parameter "t" is null` message is only a known forced-shutdown artifact when plugin loading succeeded and no earlier script error occurred.
