# Automated verification

## Python boundary

`tests/` uses `unittest` and injected bridge/platform collaborators. It covers MCP initialization/list/call, exact schemas, stdout purity, discovery and token validation, stale/dead process rejection, loopback targeting, authentication headers, response bounds, timeout/cancellation, stable errors, platform branches, and release consistency. It does not require Unreal or a network.

## Native boundary

`Private/Tests/UnrealMCPAutomationTests.cpp` compiles into development Editor builds. Seven `UnrealMCP.Phase1` cases cover the authenticated foundation. Three `UnrealMCP.Phase2` cases cover inspection. Three `UnrealMCP.Phase3` cases cover creation. Three `UnrealMCP.Phase4` cases cover the ledger, property codec, and components/defaults. Two `UnrealMCP.Phase5` cases cover K2 types and variables. `UnrealMCP.Phase6.FunctionsAndLocals` covers functions, locals, and RepNotify. `UnrealMCP.Phase7.MacrosAndCustomEvents` covers signatures, metadata, tunnels, event-graph restrictions, collisions, references, Undo/Redo, compile, and save. `UnrealMCPApiProbe.cpp` makes public-header compatibility a normal compilation requirement. Run all 20 cases through `scripts/run_headless_integration.py --automation-only`.

## Cross-process boundary

`scripts/run_headless_integration.py` uses only the disposable `ue-test/` project. It calls every released command family through the production Python client, reconciles a deliberately lost mutation response, and builds/saves a multi-component Actor Blueprint with defaults, variables, functions, locals, RepNotify, a macro, and a custom event. It then restarts the editor and verifies stable identities, exact signatures/defaults/metadata/relationships, required macro tunnels, custom-event graph placement, and the exact snapshot before checking loopback binding and clean heartbeat removal.

Generated test project assets, logs, caches, tokens, and heartbeats are never committed or used as expected-result fixtures.
