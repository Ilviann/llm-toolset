# Gameplay-framework editor

## Ownership

`FUnrealMCPGameplayFrameworkEditor` owns the narrow `gameplay_framework_edit` command. It resolves the active project's current default GameMode or GameInstance, validates one exact compatible saved class, checks project and stale-value identities, replaces only the corresponding `DefaultEngine.ini` key, verifies disk and live-settings read-back, and reports the old/new class plus restart behavior.

## Dependency direction

The bridge owns the editor and supplies the already-authenticated project hash. The component depends on `UGameMapsSettings`, exact class loading, package state, and bounded file helpers. It does not depend on Blueprint mutation, inspector cursors, arbitrary config reflection, editor UI, PIE control, or MCP framing.

## Invariants

- Only `default_game_mode` and `default_game_instance` exist; config paths, section names, keys, world overrides, server-only overrides, and arbitrary settings are never arguments.
- Every request requires the active 40-character project hash and exact current class. A mismatch returns `stale_precondition` before file access.
- Native classes must resolve exactly. Blueprint-generated classes must be compatible, compiled, saved, clean, non-skeleton, and package-backed.
- A read-only/source-controlled file rejects before writing. Persistence uses a same-directory replacement, verifies the fixed key, and restores the exact prior content after failed read-back.
- Successful assignments report `restart_required: false` and `active_sessions_unaffected: true`; existing worlds and active PIE sessions are not rewritten.

## Verification

`UnrealMCP.Phase16.FrameworkAssignment` covers stale and wrong-family rejection, saved Blueprint assignment, native restoration, and read-back for both settings. The cross-process workflow assigns both authored classes, restarts, restores the native defaults using stale-value checks, and verifies retained operation behavior through the production bridge.
