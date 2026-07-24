# Automated verification

## Python boundary

`tests/` uses `unittest` and injected bridge/platform collaborators. It covers MCP initialization/list/call, exact schemas, stdout purity, discovery and token validation, stale/dead process rejection, loopback targeting, authentication headers, response bounds, timeout/cancellation, stable errors, platform branches, release consistency, host-specific headless editor selection, and macOS-only developer-directory configuration. It does not require Unreal or a network.

## Native boundary

`Private/Tests/UnrealMCPAutomationTestSupport.h` owns shared fixture construction, argument builders, inspection helpers, snapshot tracking, save support, and cleanup conventions. Phase 17 adds `GameDataAuthoring`, bringing the suite to 30 native cases. `UnrealMCPApiProbe.cpp` keeps public structure-editor and Data Table APIs in the normal compatibility build.

Normal/adaptive and forced-unity module builds are both required. Private implementation headers include their explicit Unreal dependencies, use named internal namespaces, and remain valid when Unreal Build Tool combines family translation units.

## Cross-process boundary

`scripts/run_headless_integration.py` uses only the disposable `ue-test/` project. It derives the headless editor from the detected host, selecting the macOS app binary, the Win64 command-line editor, or the Linux editor, and requires `UNREAL_MCP_DEVELOPER_DIR` only on macOS. Each run removes its fixed-path generated fixtures before preparing them so retries do not depend on prior disposable project state. The Phase 2 fixture must report a valid post-save snapshot, then retain one exact persisted snapshot across two clean editor reloads; the production-created and edited Blueprint retains the stricter exact post-save-to-first-reload check. The workflow calls every released command family through the production Python client, reconciles deliberately lost component and every graph-edit operation family, and builds/saves a multi-component Actor Blueprint with defaults, a variable, functions, locals, RepNotify, a macro, a custom event, positioned graph nodes, a typed pin default, direct execution behavior, live wildcard specialization through any context-valid wildcard operator (preferring Add when present), an explicit conversion node, and BeginPlay-driven PrintString behavior. It then restarts the editor and verifies stable identities, exact signatures/defaults/metadata/relationships, required macro tunnels, custom-event graph placement, surviving and removed graph nodes, the wildcard/conversion path, exact snapshot, every released action family, representative serialized catalog size, loopback binding, and clean heartbeat removal.

The cross-process workflow also authors every gameplay-framework family, typed Actor replication, a reliable server RPC, assigned default GameMode/GameInstance classes, one weapon-stat user-defined struct, and one typed balance table. It restarts, restores the native framework defaults with exact stale checks, and verifies RPC/default/schema/row identities and values.

The Windows workflow currently reaches the first clean restart but fails the exact snapshot comparison for the Blueprint created through the production bridge. This confirmed unfixed error is tracked as [`issue-1`](../issues/issue-1.md).
