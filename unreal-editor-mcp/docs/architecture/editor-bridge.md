# Unreal editor bridge

## Ownership

`plugin/UnrealMCP/Source/UnrealMCP/` is one editor-only module. `UnrealMCPModule.cpp` is the composition root and owns configured-port validation, stale-record cleanup, token startup gating, project hashing, loopback listener configuration, and bridge lifetime. `UnrealMCPTokenStore` owns credential persistence. `UnrealMCPBridge` owns the HTTP route, authentication, request queue, Game-thread dispatch, command composition, and heartbeat. `UnrealMCPBlueprintInspector` owns bounded Actor Blueprint reads. `UnrealMCPBlueprintMutator` owns Phase 3 creation, compile, save, cleanup, and read-back orchestration. `UnrealMCPProtocol` owns wire parsing and bounded envelopes. `UnrealMCPDiscovery` owns the non-secret heartbeat. `UnrealMCPCompatibility` contains platform and Unreal-version branches.

## Dependency direction

The module constructs the token store and bridge. The bridge calls the protocol and discovery helpers but none of those helpers knows about module lifetime. Unreal object/editor state is read only after `AsyncTask` reaches the Game thread. The HTTP handler performs authentication, body bounds, shape validation, command allowlisting, and queue admission before dispatch.

## Security and lifecycle invariants

- Startup fails closed unless the token is valid, atomically persisted, permission-restricted where supported, and re-read exactly.
- The per-port HTTPServer override binds `127.0.0.1`; startup verifies the listener became active.
- Authentication uses a fixed-work comparison and precedes JSON parsing.
- The route accepts POST at `/unreal-mcp/v1/command`; the command allowlist contains the six commands released through Phase 3.
- At most eight requests are queued, dispatch expires after five seconds, and responses are at most 256 KiB.
- The discovery record never contains a token or project path and is atomically refreshed every two seconds.
- Shutdown stops heartbeats, removes discovery, unbinds the route, releases the router, clears the in-memory token, and causes retained requests to return cancellation.

Unreal's HTTPServer owns listener sockets process-wide. The plugin owns and unbinds only its route; the shared module closes listener sockets during engine shutdown. This avoids stopping unrelated HTTPServer users during a dynamic plugin unload.

## Public Unreal boundary

`UnrealMCPApiProbe.cpp` is compiled in every build and includes only public headers for HTTPServer/router, `FScopedTransaction`, Kismet/Blueprint utilities, Subobject Data, K2 schema and spawners, compiler logs, Asset Registry, and package saving. Unreal 5.8 is the first compatibility branch; later branches belong only in `UnrealMCPCompatibility` and require a test.

## Verification

Compile the disposable Editor target, run `UnrealMCP.Phase1` Automation Tests, then run `scripts/run_headless_integration.py` as documented in the README.
