# Unreal Editor MCP tool guides

This page is the navigation entry point for every released tool family. For installation, first connection, and the concise contract overview, start with the [project README](../../README.md).

Unreal Editor MCP 0.34.0 is an offline-first MCP bridge for Unreal Engine 5.7.x. It pairs a dependency-free Python 3.10+ stdio server with an editor-only C++ plugin. Readonly mode is the default and exposes these nine tools in deterministic order:

- `capabilities` always reports the configured project name/hash and Python surface. With an active bridge it also reports exact plugin/Unreal versions, commands, features, listener state, effective limits, and the Blueprint-family matrix; otherwise `native_capabilities_available` and `bridge_ready` are false and native-only fields are absent.
- `editor_state` reports project identity, bridge readiness, play/simulate/save/GC state, and concise queued-operation state.
- `operation_status` looks up one retained operation by operation and bridge identity without cancelling it.
- `asset_references` finds bounded Asset Registry and live-memory referencers for one exact mounted asset without loading candidate packages.
- `level_inspect` discovers mounted World assets, reports the current map snapshot, pages World Partition actor descriptors, and inspects exact actor/component properties.
- `level_open` safely opens one exact mounted World asset through the retained operation ledger without saving, discarding, or dirtying project content.
- `blueprint_inspect` discovers every published Blueprint family across mounted content and returns bounded pages of one selected Blueprint's structure, including effective inherited component-template values and provenance.
- `blueprint_action_catalog` discovers bounded context-valid function, variable, event, flow-control, cast, literal, and operator actions for one exact Blueprint graph snapshot.
- `game_data_inspect` reads one user-defined struct schema, bounded typed Data Table row page, or bounded read-only Data Asset property page from an exact asset snapshot.

Starting the server with `--writable` is an explicit trust decision and exposes twenty-five tools. It inserts `operation_cancel` immediately after `operation_status`, then adds these project-content mutation tools in their established family order:

- `operation_cancel` safely requests cancellation of one queued or preflight retained mutation by operation and bridge identity.
- `asset_delete` deletes one exact unreferenced project asset package or complete inactive-map package closure after retained stale-safe preflight and persistence verification.
- `level_manage` creates or configures and reload-verifies one exact project map with bounded World Settings.
- `level_actor_edit` prevalidates and transactionally applies one bounded stale-safe Actor/component batch without saving.
- `level_save` explicitly saves the returned current-map package set and verifies requested identities and values by inspection or reload.
- `blueprint_graph_edit` creates, moves, removes, configures, or connects graph nodes and pins, including wildcard specialization and explicitly requested bounded conversions.
- `blueprint_block_replace` atomically replaces one complete user-owned function after an isolated scratch compile and exact boundary preconditions.
- `blueprint_create` creates, compiles, saves, and verifies one new supported Blueprint family without overwriting content.
- `blueprint_compile` explicitly compiles one mutable supported-family Blueprint and returns bounded diagnostics.
- `blueprint_save` explicitly saves one mutable supported-family Blueprint package without interactive dialogs.
- `blueprint_component_edit` adds, removes, renames, reparents, roots, or edits one local Actor component.
- `blueprint_default_edit` edits one supported Blueprint-generated class-default property.
- `blueprint_member_edit` adds, renames, updates, or safely removes one typed Blueprint variable, function, local variable, macro, or custom-event shell.
- `gameplay_framework_edit` assigns only the active project's default GameMode or GameInstance class with stale-value and project-identity preconditions.
- `game_data_edit` creates or atomically edits user-defined structs and typed Data Table rows with validation, saving, and read-back.

`--editor-lifecycle <absolute-executable>` independently appends `editor_lifecycle`, producing ten readonly-with-lifecycle tools or twenty-six writable-with-lifecycle tools. It provides configured launch, safe graceful shutdown, durable restart, and lifecycle cancellation. CSV/JSON filesystem import/export, Curve Tables, Data Asset mutation, arbitrary UObject assets, supplied struct code, General Project Settings beyond the narrow framework operation, unrestricted world overrides, runtime server/client control, builds, Blueprint reparenting, console access, unrestricted reflection, forced process termination, and code execution remain unavailable.

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

## User-defined structs, Data Tables, and Data Assets

See [Gameplay frameworks and data](gameplay-and-data.md#user-defined-structs-data-tables-and-data-assets).

## Multiplayer authoring and framework assignment

See [Gameplay frameworks and data](gameplay-and-data.md#multiplayer-authoring-and-framework-assignment).

## Limits

See [Limits and offline testing](limits-and-testing.md#limits).

## Offline development and tests

See [Limits and offline testing](limits-and-testing.md#offline-development-and-tests).
