# Runtime debugger and gameplay validation

## Purpose

Observe and validate a debug run through Godot's debugger channel without adding a game-side socket or general execution/mutation surface.

## Owned source

- Editor side: `runtime_debugger_gateway.gd`, `runtime_scene_inspector.gd`, `runtime_gameplay_commands.gd`.
- Debug-run side: `runtime_probe.gd`, `runtime_identity_context.gd`, `runtime_tree_service.gd`, `runtime_capture_service.gd`, `runtime_input_service.gd`, `runtime_condition_service.gd`.

## Dependencies

Uses shared identities, paths, values, cursors, limits, and errors plus the current run identity from editor state. The plugin bridge retains validated deferred requests. Edited inspection delegates runtime scope here. Python dispatch validates staged captures and exposes image results.

## Invariants

- The probe activates only with `EngineDebugger`; exports and ordinary non-debug runs expose nothing.
- There is no game-side network listener, arbitrary mutation, arbitrary method call, expression evaluation, or supplied-code execution.
- One active debugger session is supported; all requests/responses bind project, run, session, probe version, nonce, command, request, and limits.
- Pending/deferred responses, timeouts, traversals, evidence, captures, and held inputs are independently bounded.
- Runtime node identities and cursors reject replacement runs, sessions, objects, and snapshots.
- Capture uses a fixed derived `.godot/godot_mcp/captures/<id>.png` staging path and cleanup policy.
- Input accepts only existing Input Map actions and always schedules/releases bounded holds.
- Conditions use only the fixed play/node/count/built-in-scalar grammar.

## Change and verification guide

Treat probe protocol, handshake fields, commands, limits, deferred markers, and capture records as cross-process contracts. Run Python contracts/server tests, Phase 9 runtime inspection, Phase 10 gameplay validation, Phase 13 boundary hardening, plugin load, and subprocess integration.
