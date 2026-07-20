# Plugin lifecycle, bridge, discovery, and routing

## Purpose

Compose editor-side services, start them only after credential persistence succeeds, expose an authenticated bounded localhost bridge, publish discovery, and route each command to exactly one owner.

## Owned source

- `plugin/addons/godot_mcp/godot_mcp.gd` — `EditorPlugin` composition root, capabilities, polling, and runtime-probe lifecycle.
- `authenticated_startup.gd` and `token_store.gd` — fail-closed credential gate.
- `bridge_server.gd` — bounded localhost clients and immediate/deferred responses.
- `command_router.gd` — atomic duplicate-safe handler registration and dispatch.
- `discovery_record.gd` — project-scoped heartbeat publication.

## Dependencies

This is the Godot composition layer. It constructs shared infrastructure, editor-state services, scene/project command services, and the runtime debugger gateway. It is contract-coupled to Python tool policy, bridge transport, discovery, errors, limits, and versions.

## Invariants

- Token load/generation/write/flush must succeed before listener, services, or discovery start.
- The bridge listens only on `127.0.0.1`, owns one configured port, caps active clients, and deadlines incomplete authentication/requests.
- Token comparison and all request/response buffers are bounded.
- Registration is atomic; duplicate command ownership prevents startup rather than selecting an arbitrary handler.
- A client is retained only for the private validated debugger-deferred marker.
- Discovery is periodically refreshed and removed only by its owning process.
- Capabilities report executable command, feature, limit, Godot, and version state.
- The reserved runtime probe autoload is installed and removed only when its exact name/path ownership matches.

## Change and verification guide

New Godot services must expose a focused handler map and receive only collaborators they call. Update Python expected contracts, capabilities, and command ownership tests together. Run the router and boundary-hardening headless tests, Python contract/bridge tests, plugin load check, and subprocess integration for end-to-end changes.
