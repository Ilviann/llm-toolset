# Unreal editor bridge

## Ownership

`plugin/UnrealMCP/Source/UnrealMCP/` is the stable editor host module. `UnrealMCPModule.cpp` is the composition root and owns built-in domain loading, configured-port validation, stale-record cleanup, token startup gating, project hashing, loopback listener configuration, and bridge lifetime. `UnrealMCPTokenStore` owns credential persistence. `UnrealMCPBridge` owns the HTTP route, authentication, request queue, Game-thread dispatch, operation-ledger integration, final capability envelope, heartbeat, and process identity. `UnrealMCPCommandCatalog` owns fixed command composition and freeze; Asset Core, Blueprint, UMG, and Content modules contribute their own handlers, features, limits, families, services, and tests through the typed domain registrar. `UnrealMCPWireTypes`, `UnrealMCPJsonCodec`, and `UnrealMCPOperationLedger` live in Asset Core and provide the shared inward runtime boundary.

## Dependency direction

The host loads built-in domains, freezes their asset families and command contributions, and then starts the bridge listener. The protocol decodes HTTP JSON into neutral records before queueing, domain handlers consume and return only neutral records, and the protocol encodes the final envelope after dispatch. The companion registry is the sole temporary base-domain adapter to companion API v1 JSON. The bridge calls the catalog, protocol, ledger, and discovery helpers but none of those helpers knows about host module lifetime. Unreal object/editor state is read only after `AsyncTask` reaches the Game thread. The HTTP handler performs authentication, body bounds, shape validation, fixed-catalog lookup, ledger/queue admission, and dispatch.

## Security and lifecycle invariants

- Startup fails closed unless the token is valid, atomically persisted, permission-restricted where supported, and re-read exactly.
- The per-port HTTPServer override binds `127.0.0.1`; startup verifies the listener became active.
- Authentication uses a fixed-work comparison and precedes JSON parsing.
- The route accepts POST at `/unreal-mcp/v1/command`; the command allowlist contains the twenty-five model-facing commands released through `readonly-mode` plus internal `editor_shutdown`. Python access configuration determines which of those model-facing commands are published and dispatchable through MCP.
- At most eight requests are queued, dispatch expires after five seconds, and responses are at most 256 KiB.
- Mutation IDs are admitted before Game-thread dispatch, bound to the command, canonical arguments, project/authenticated context, and bridge instance. Terminal results are retained before responding.
- The process-scoped ledger retains at most 128 operations for 15 minutes. Same-request replay is non-executing; conflicting ID reuse rejects; queued cancellation is safe; another bridge instance resolves as `outcome_unknown`.
- The discovery record never contains a token or project path and is atomically refreshed every two seconds.
- Shutdown stops heartbeats, removes discovery, unbinds the route, releases the router, clears the in-memory token, and causes retained requests to return cancellation.
- Authenticated `editor_shutdown` accepts no arguments and refuses PIE/simulation, saves, garbage collection, active transactions, asset compilation, and dirty packages before scheduling a non-forced engine exit.

Unreal's HTTPServer owns listener sockets process-wide. The plugin owns and unbinds only its route; the shared module closes listener sockets during engine shutdown. This avoids stopping unrelated HTTPServer users during a dynamic plugin unload.

## Public Unreal boundary

`UnrealMCPApiProbe.cpp` is compiled in every build and includes only public headers for HTTPServer/router, `FScopedTransaction`, Kismet/Blueprint utilities, Subobject Data, K2 schema and spawners, compiler logs, Asset Registry, and package saving. Function probes take addresses rather than relying on permissive function-name expressions, and overloaded public functions use an explicit expected signature so Clang and MSVC verify the same boundary. Unreal 5.8 is the first compatibility branch; later branches belong only in `UnrealMCPCompatibility` and require a test.

## Verification

Compile the disposable Editor target, run all `UnrealMCP` Automation Tests, then run `scripts/run_headless_integration.py` as documented in the README.
