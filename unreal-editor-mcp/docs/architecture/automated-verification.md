# Automated verification

## Python boundary

`tests/` uses `unittest` and injected bridge/platform collaborators. It covers MCP initialization/list/call, exact schemas, stdout purity, discovery and token validation, stale/dead process rejection, loopback targeting, authentication headers, response bounds, timeout/cancellation, stable errors, platform branches, and release consistency. It does not require Unreal or a network.

## Native boundary

`Private/Tests/UnrealMCPAutomationTests.cpp` compiles into development Editor builds. The seven-case `UnrealMCP.Phase1` group covers token persistence, invalid-token fail-closed behavior, constant-work comparisons, parser and live-route request bounds, authentication rejection, duplicate route ownership, error-envelope bounds, the active compatibility branch, and production Game-thread dispatch. `UnrealMCPApiProbe.cpp` makes public-header compatibility a normal compilation requirement. Run the group through `scripts/run_headless_integration.py --automation-only`; its captured output avoids macOS GUI-launch behavior that can make direct pipe-based runners unreliable.

## Cross-process boundary

`scripts/run_headless_integration.py` uses only the disposable `ue-test/` project. It starts Unreal with the plugin, waits for a fresh live-process heartbeat, calls both commands through the production Python client, rejects a deliberately wrong token, proves the port is unreachable through a non-loopback local interface, terminates the editor, and rejects any remaining stale heartbeat.

Generated test project assets, logs, caches, tokens, and heartbeats are never committed or used as expected-result fixtures.
