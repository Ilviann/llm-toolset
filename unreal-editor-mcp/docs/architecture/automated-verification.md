# Automated verification

## Python boundary

`tests/` uses `unittest` and injected bridge/platform collaborators. It covers MCP initialization/list/call, exact schemas, stdout purity, discovery and token validation, stale/dead process rejection, loopback targeting, authentication headers, response bounds, timeout/cancellation, stable errors, platform branches, and release consistency. It does not require Unreal or a network.

## Native boundary

`Private/Tests/UnrealMCPAutomationTestSupport.h` owns shared fixture construction, argument builders, inspection helpers, snapshot tracking, save support, and cleanup conventions. Phase 16 adds `MultiplayerAuthoring` and `FrameworkAssignment`, bringing the suite to 29 native cases. `UnrealMCPApiProbe.cpp` keeps public-header compatibility a normal compilation requirement.

Normal/adaptive and forced-unity module builds are both required. Private implementation headers include their explicit Unreal dependencies, use named internal namespaces, and remain valid when Unreal Build Tool combines family translation units.

## Cross-process boundary

`scripts/run_headless_integration.py` uses only the disposable `ue-test/` project. It calls every released command family through the production Python client, reconciles deliberately lost component and every graph-edit operation family, and builds/saves a multi-component Actor Blueprint with defaults, a variable, functions, locals, RepNotify, a macro, a custom event, positioned graph nodes, a typed pin default, direct execution behavior, live wildcard specialization, an explicit conversion node, and BeginPlay-driven PrintString behavior. It then restarts the editor and verifies stable identities, exact signatures/defaults/metadata/relationships, required macro tunnels, custom-event graph placement, surviving and removed graph nodes, the wildcard/conversion path, exact snapshot, every released action family, representative serialized catalog size, loopback binding, and clean heartbeat removal.

The cross-process workflow also authors every gameplay-framework family, typed Actor replication, a reliable server RPC, and assigned default GameMode/GameInstance classes. It restarts, restores the native defaults with exact stale checks, and verifies RPC/default identities.
