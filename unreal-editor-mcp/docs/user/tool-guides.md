# Unreal Editor MCP tool guides

This page is the navigation entry point for every released tool family. For installation, first connection, and the concise contract overview, start with the [project README](../../README.md).

Unreal Editor MCP 0.27.0 is an offline-first MCP bridge for Unreal Engine 5.8+. It pairs a dependency-free Python 3.10+ stdio server with an editor-only C++ plugin. Default mode exposes exactly twenty-four tools:

- `capabilities` always reports the configured project name/hash and Python surface. With an active bridge it also reports exact plugin/Unreal versions, commands, features, listener state, effective limits, and the Blueprint-family matrix; otherwise `native_capabilities_available` and `bridge_ready` are false and native-only fields are absent.
- `editor_state` reports project identity, bridge readiness, play/simulate/save/GC state, and concise queued-operation state.
- `operation_status` reconciles or safely cancels one retained mutation by operation and bridge identity.
- `asset_references` finds bounded Asset Registry and live-memory referencers for one exact mounted asset without loading candidate packages.
- `asset_delete` deletes one exact unreferenced project asset package or complete inactive-map package closure after retained stale-safe preflight and persistence verification.
- `level_inspect` discovers mounted World assets, reports the current map snapshot, pages World Partition actor descriptors, and inspects exact actor/component properties.
- `level_open` safely opens one exact mounted World asset through the retained mutation ledger.
- `level_manage` creates or configures and reload-verifies one exact project map with bounded World Settings.
- `level_actor_edit` prevalidates and transactionally applies one bounded stale-safe Actor/component batch without saving.
- `level_save` explicitly saves the returned current-map package set and verifies requested identities and values by inspection or reload.
- `blueprint_inspect` discovers every published Blueprint family across mounted content and returns bounded pages of one selected Blueprint's structure.
- `blueprint_action_catalog` discovers bounded context-valid function, variable, event, flow-control, cast, literal, and operator actions for one exact Blueprint graph snapshot.
- `blueprint_graph_edit` creates, moves, removes, configures, or connects graph nodes and pins, including wildcard specialization and explicitly requested bounded conversions.
- `blueprint_block_replace` atomically replaces one complete user-owned function after an isolated scratch compile and exact boundary preconditions.
- `blueprint_create` creates, compiles, saves, and verifies one new supported Blueprint family without overwriting content.
- `blueprint_compile` explicitly compiles one mutable supported-family Blueprint and returns bounded diagnostics.
- `blueprint_save` explicitly saves one mutable supported-family Blueprint package without interactive dialogs.
- `blueprint_component_edit` adds, removes, renames, reparents, roots, or edits one local Actor component.
- `blueprint_default_edit` edits one supported Blueprint-generated class-default property.
- `blueprint_member_edit` adds, renames, updates, or safely removes one typed Blueprint variable, function, local variable, macro, or custom-event shell.
- `gameplay_framework_edit` assigns only the active project's default GameMode or GameInstance class with stale-value and project-identity preconditions.
- `game_data_inspect` reads one user-defined struct schema or bounded page of typed Data Table rows from an exact asset snapshot.
- `game_data_edit` creates or atomically edits user-defined structs and typed Data Table rows with validation, saving, and read-back.

Opt-in large mode adds a twenty-fifth tool, `editor_lifecycle`, for configured launch, safe graceful shutdown, durable restart, and cancellation. CSV/JSON filesystem import/export, Curve Tables, Data Assets, arbitrary UObject assets, supplied struct code, General Project Settings beyond the narrow framework operation, unrestricted world overrides, runtime server/client control, builds, Blueprint reparenting, console access, unrestricted reflection, forced process termination, and code execution remain unavailable.

## Task guides

- [Setup and operation](setup-and-operation.md)
- [Levels and assets](levels-and-assets.md)
- [Blueprint inspection](blueprint-inspection.md)
- [Blueprint graph authoring](graph-authoring.md)
- [Blueprint mutation](blueprint-mutation.md)
- [Gameplay frameworks and data](gameplay-and-data.md)
- [Limits and offline testing](limits-and-testing.md)
- [Widget Blueprint authoring](widget-blueprints.md)

The following headings preserve links to sections that previously lived on this page.

## Security model

See [Setup and operation](setup-and-operation.md#security-model).

## Install

See [Setup and operation](setup-and-operation.md#install).

## LM Studio

See [Setup and operation](setup-and-operation.md#lm-studio).

## Optional editor lifecycle

See [Setup and operation](setup-and-operation.md#optional-editor-lifecycle).

## Level discovery and opening

See [Levels and assets](levels-and-assets.md#level-discovery-and-opening).

## Level management

See [Levels and assets](levels-and-assets.md#level-management).

## Asset references

See [Levels and assets](levels-and-assets.md#asset-references).

## Asset deletion

See [Levels and assets](levels-and-assets.md#asset-deletion).

## Blueprint-family inspection

See [Blueprint inspection](blueprint-inspection.md#blueprint-family-inspection).

## Blueprint action catalog

See [Blueprint graph authoring](graph-authoring.md#blueprint-action-catalog).

## Graph-node lifecycle

See [Blueprint graph authoring](graph-authoring.md#graph-node-lifecycle).

## Complete function replacement

See [Blueprint graph authoring](graph-authoring.md#complete-function-replacement).

## Complete atomic pin and connection editing

See [Blueprint graph authoring](graph-authoring.md#complete-atomic-pin-and-connection-editing).

## Reliable Actor Blueprint mutation

See [Blueprint mutation](blueprint-mutation.md#reliable-actor-blueprint-mutation).

## Creation, components, defaults, compile, and save

See [Blueprint mutation](blueprint-mutation.md#creation-components-defaults-compile-and-save).

## Blueprint member variables

See [Blueprint mutation](blueprint-mutation.md#blueprint-member-variables).

## Blueprint functions and local variables

See [Blueprint mutation](blueprint-mutation.md#blueprint-functions-and-local-variables).

## Blueprint macros and custom events

See [Blueprint mutation](blueprint-mutation.md#blueprint-macros-and-custom-events).

## GameMode and GameState families

See [Gameplay frameworks and data](gameplay-and-data.md#gamemode-and-gamestate-families).

## GameInstance family

See [Gameplay frameworks and data](gameplay-and-data.md#gameinstance-family).

## User-defined structs and Data Tables

See [Gameplay frameworks and data](gameplay-and-data.md#user-defined-structs-and-data-tables).

## Multiplayer authoring and framework assignment

See [Gameplay frameworks and data](gameplay-and-data.md#multiplayer-authoring-and-framework-assignment).

## Limits

See [Limits and offline testing](limits-and-testing.md#limits).

## Offline development and tests

See [Limits and offline testing](limits-and-testing.md#offline-development-and-tests).
