# Automated verification

## Python boundary

`tests/` uses `unittest` and injected bridge/platform collaborators. It covers MCP initialization/list/call, exact schemas, stdout purity, discovery and token validation, stale/dead process rejection, loopback targeting, authentication headers, response bounds, timeout/cancellation, stable errors, platform branches, and release consistency. It does not require Unreal or a network.

## Native boundary

`Private/Tests/UnrealMCPAutomationTestSupport.h` owns shared fixture construction, argument builders, inspection helpers, snapshot tracking, save support, and cleanup conventions. Phase-specific translation units contain independently registered cases under their `UnrealMCP.PhaseN` filters. Seven Phase 1 cases cover the authenticated foundation; three Phase 2 inspection; three Phase 3 creation; three Phase 4 ledger/property/component-default; two Phase 5 K2/member; and the focused Phase 6, 7, 8, 10, 11, and 12 scenarios cover functions/locals/RepNotify, macros/custom events, core/expanded action catalogs, graph-node lifecycle, and pin defaults/direct connections. `UnrealMCPApiProbe.cpp` keeps public-header compatibility a normal compilation requirement. Run all 24 cases through `scripts/run_headless_integration.py --automation-only`.

Normal/adaptive and forced-unity module builds are both required. Private implementation headers include their explicit Unreal dependencies, use named internal namespaces, and remain valid when Unreal Build Tool combines family translation units.

## Cross-process boundary

`scripts/run_headless_integration.py` uses only the disposable `ue-test/` project. It calls every released command family through the production Python client, reconciles deliberately lost component and all six graph-edit responses, and builds/saves a multi-component Actor Blueprint with defaults, variables, functions, locals, RepNotify, a macro, a custom event, positioned graph nodes, a typed pin default, and direct execution behavior. It then restarts the editor and verifies stable identities, exact signatures/defaults/metadata/relationships, required macro tunnels, custom-event graph placement, surviving node/pin identities and position, removed-node absence, the pin default and connection, the exact snapshot, every released action family, wildcard operators, representative serialized catalog size, loopback binding, and clean heartbeat removal.

Generated test project assets, logs, caches, tokens, and heartbeats are never committed or used as expected-result fixtures.
