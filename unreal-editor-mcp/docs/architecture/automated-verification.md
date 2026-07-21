# Automated verification

## Python boundary

`tests/` uses `unittest` and injected bridge/platform collaborators. It covers MCP initialization/list/call, exact schemas, stdout purity, discovery and token validation, stale/dead process rejection, loopback targeting, authentication headers, response bounds, timeout/cancellation, stable errors, platform branches, and release consistency. It does not require Unreal or a network.

## Native boundary

`Private/Tests/UnrealMCPAutomationTests.cpp` compiles into development Editor builds. Seven `UnrealMCP.Phase1` cases cover the authenticated foundation. Three `UnrealMCP.Phase2` cases cover inspection. Three `UnrealMCP.Phase3` cases cover creation policy, compile/save behavior, cleanup, diagnostics, local-plugin confinement, and a persisted creation fixture. Three `UnrealMCP.Phase4` cases cover the operation ledger, shared property codec, and component/default edits. `UnrealMCPApiProbe.cpp` makes public-header compatibility a normal compilation requirement. Run all 16 cases through `scripts/run_headless_integration.py --automation-only`; its captured output avoids macOS GUI-launch behavior that can make direct pipe-based runners unreliable.

## Cross-process boundary

`scripts/run_headless_integration.py` uses only the disposable `ue-test/` project. It prepares the inspection fixture, starts an editor, calls every released command family through the production Python client, deliberately discards a component-edit response, reconciles and replays the retained result, builds and saves a multi-component Actor Blueprint with an edited class default, rejects a wrong token, and proves loopback-only listening. It then restarts the editor and verifies parent, compile state, hierarchy, default, and exact snapshot before checking clean heartbeat removal.

Generated test project assets, logs, caches, tokens, and heartbeats are never committed or used as expected-result fixtures.
