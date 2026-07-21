# Automated verification

## Python boundary

`tests/` uses `unittest` and injected bridge/platform collaborators. It covers MCP initialization/list/call, exact schemas, stdout purity, discovery and token validation, stale/dead process rejection, loopback targeting, authentication headers, response bounds, timeout/cancellation, stable errors, platform branches, and release consistency. It does not require Unreal or a network.

## Native boundary

`Private/Tests/UnrealMCPAutomationTests.cpp` compiles into development Editor builds. Seven `UnrealMCP.Phase1` cases cover the authenticated foundation. Three `UnrealMCP.Phase2` cases create behavioral Blueprint fixtures and cover inspection contracts, non-mutation, inherited and empty content, graph ceilings, value support, cursors, identity behavior, compile, undo, and save. `UnrealMCPApiProbe.cpp` makes public-header compatibility a normal compilation requirement. Run all ten cases through `scripts/run_headless_integration.py --automation-only`; its captured output avoids macOS GUI-launch behavior that can make direct pipe-based runners unreliable.

## Cross-process boundary

`scripts/run_headless_integration.py` uses only the disposable `ue-test/` project. It first creates and saves a Blueprint fixture through native behavioral code, then starts a fresh editor, waits for a live heartbeat, calls all released command families through the production Python client, compares the reloaded structural snapshot, rejects a deliberately wrong token, proves the port is unreachable through a non-loopback local interface, terminates the editor, and rejects any remaining stale heartbeat.

Generated test project assets, logs, caches, tokens, and heartbeats are never committed or used as expected-result fixtures.
