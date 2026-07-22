# Unreal Editor MCP history

## 0.16.0 — 2026-07-23

- Added `game_data_inspect` and reconciled `game_data_edit` for bounded user-defined struct schemas and typed Data Tables, bringing the released surface to fifteen exact tools.
- Added stable struct-member identities, safe schema evolution, bounded dependency rejection for destructive changes, typed row creation and inspection, and transactional add/replace/rename/remove/batch row authoring.
- Added one reflected row-value codec for scalars, enums, common/native/user structs, arrays, sets, maps, and compatible references with strict depth, field, collection, row, dependency, mutation-scope, and unsupported-property limits.
- Added package saving, snapshot/cursor/replay contracts, explicit failure restoration, Python schema coverage, native Phase 17 Automation coverage, public API probes, architecture/type documentation, and a focused shooter-balance workflow example.
- Verified Phase 17 with Python contract tests, Unreal 5.8 native Automation, normal and forced-unity builds, and the production bridge restart workflow.

## 0.15.0 — 2026-07-23

- Added exact custom-event RPC metadata and inspection for non-replicated, server, owning-client, and multicast modes with live family restrictions and reliable/unreliable semantics.
- Added bounded typed Actor and component replication edits, dependency validation, live multiplayer capability records, and retained replicated-variable/RepNotify coupling.
- Added reconciled `gameplay_framework_edit` for only the active project's default GameMode and GameInstance, using exact saved compatible classes, stale current-value checks, verified config replacement, and failure restoration.
- Added a multiplayer/framework workflow example, Python schema/release coverage, two Phase 16 native Automation cases, and cross-process assignment/restart/restore verification.

## 0.14.0 — 2026-07-22

- Added `UGameInstance` as an explicit `uobject_derived` Blueprint family while preserving the existing Actor, GameMode, and GameState classifications and rejecting arbitrary UObject families.
- Published GameInstance support for creation, inspection, class defaults, variables, functions/locals, macros, custom events, action cataloging, graph editing, compile, save, diagnostics, and reconciliation, with components explicitly false in both published and live capabilities.
- Added stable pre-mutation `invalid_component` rejection for GameInstance component requests, truthful non-Actor summary output, GameInstance session-default editing, and Init/Shutdown callback action and graph-node coverage.
- Added a focused GameInstance example, Phase 15 native Automation coverage, Python release-contract checks, and cross-process creation/edit/compile/save/restart verification.

## 0.13.0 — 2026-07-22

- Added one explicit shared Blueprint-family policy for generic Actor, GameModeBase, GameMode, GameStateBase, and GameState lineages, replacing scattered Actor-only eligibility checks across discovery, inspection, creation, mutation, cataloging, and graph editing.
- Published the ordered family/operation matrix through `capabilities`, including explicit exclusions for Blueprint parent changes and project-settings assignment; discovery and every exact operation now report the resolved family, and inspection/mutation results report live family capabilities.
- Reused the complete Actor-derived authoring path for all four gameplay-framework families, including safe class defaults, local components, variables, function locals, callbacks/actions, graph editing, compile/save, diagnostics, reconciliation, and restart read-back.
- Added family-specific examples, public API probes, Python release-contract coverage, Phase 14 native Automation coverage, and cross-process creation/save/restart verification for all four families.

## 0.12.0 — 2026-07-22

- Completed atomic Actor Blueprint graph editing with live-schema wildcard specialization, numeric promotion, pin/node reconstruction tracking, and directed-cycle rejection.
- Added explicit per-connection `automatic_conversion` opt-in, disabled by omission, with one-node preflight capacity, transactional insertion, stable node/pin identities, conversion-path read-back, and rollback for unexpected insertions.
- Standardized graph change records with operation discriminators, created/reconstructed identity arrays, changed-node records, direct/conversion/specialization flags, and published conversion limits.
- Added exact schema coverage, Phase 13 native Automation coverage, and a lost-response/restart MCP acceptance workflow that authors and verifies BeginPlay-driven Actor behavior through components, a variable, direct links, a wildcard operator, and an explicit conversion.

## 0.11.0 — 2026-07-22

- Added reconciled `blueprint_graph_edit` operations for typed pin defaults and schema-valid direct pin connection/disconnection by stable graph, node, and pin identities.
- Parse defaults through the shared bounded K2 codec and live graph schema, including Boolean, numeric, textual, object, class, asset, and engine-default forms; protected, linked, hidden, unsupported, stale, and oversized defaults reject before a transaction.
- Honor the live schema's exact make and link-replacement responses while rejecting incompatible, wildcard-specializing, promotion, and conversion-node connections; automatic conversion remains disabled.
- Added 64-link-per-pin and 512-character canonical-default limits, typed pin-default inspection, object/text-inclusive snapshots, capability flags, native Undo/Redo/compile/save coverage, and cross-process lost-response/restart verification for all three operations.

## 0.10.0 — 2026-07-22

- Added reconciled `blueprint_graph_edit` operations for action-ID-backed node creation, exact movement, and safe removal in local event, user-function, and macro graphs.
- Re-resolve and re-filter retained action signatures against the live graph immediately before invocation; created nodes and pins receive persistent GUID identities, while unique spawners that return an existing node are reported explicitly.
- Added graph/node/snapshot preconditions, protected-graph and protected-node rejection, mutation-scope enforcement, transactional restoration, authoritative read-back, and published graph-node, pin, and coordinate bounds.
- Added exact Python schemas and stable graph errors, native Phase 11 coverage for lifecycle families/failures/Undo/Redo/compile/save, and cross-process lost-response reconciliation plus restart verification for every lifecycle operation.

## 0.9.0 — 2026-07-22

- Expanded `blueprint_action_catalog` with context-valid event, flow-control, cast, literal, and common operator families while preserving the Phase 8 opaque-ID, cache, expiry, snapshot, scan, and mutation-free contracts.
- Added explicit wildcard, latent, and class-cast metadata; exact function filtering for function-backed event/literal/operator actions; target-class filtering for casts; and live suppression of pre-existing unique events.
- Applied Unreal's live Blueprint, graph, and optional pin filters across event, function, and macro graphs, including latent restrictions and wildcard operator specialization candidates.
- Added exact Python schemas, focused examples, public-API probes, native Phase 10 Automation coverage, representative context-size checks, and cross-process restart verification for every released action family.

## 0.8.1 — 2026-07-21

- Split the native Blueprint mutator into lifecycle, component/default, member, function, local-variable, macro, and custom-event translation units while preserving its bridge-facing facade and injected compile/save seams.
- Added one bounded typed Blueprint-reference scanner shared by inspection and mutation decisions, with JSON encoding confined to inspection and mutation result boundaries.
- Decomposed inspection into a typed normalized query, focused overview/member/callable/macro/custom-event/graph collectors, one snapshot fingerprint flow, and a cursor-only inspector facade.
- Separated action-catalog query decoding, family classification/scanning, result encoding, and retained-cache orchestration to provide the Phase 10 family extension seam.
- Split native Automation Tests by phase with shared fixture and argument support, and verified normal/adaptive and forced-unity builds, all native cases, and the cross-process workflow.

## 0.8.0 — 2026-07-21

- Added read-only `blueprint_action_catalog` for context-valid pure/impure, static/instance function calls and variable get/set actions in one exact Actor Blueprint graph snapshot.
- Added exact text, owner-class, function, member, family, graph, and pin-context filters backed by Unreal's public action database, spawners, and live action filter.
- Added bridge/class/schema/snapshot/query-bound opaque action IDs with deterministic cache reuse, 60-second expiry, bounded retention, eviction, restart invalidation, and no Phase 8 invocation path.
- Added target-first bounded scanning, observable truncation/timeouts, published limits, exact Python schema coverage, native non-mutation Automation coverage, examples, and cross-process restart verification.

## 0.7.0 — 2026-07-21

- Added bounded macro and custom-event inspection with stable graph/node identities, exact signatures and parameter defaults, metadata, ownership distinctions, graph relationships, required nodes, and reference summaries.
- Extended `blueprint_member_edit` with transactional macro and custom-event add, identity-preserving rename, supported update, and reject-only safe removal operations while keeping the ten-tool surface unchanged.
- Added event-graph targeting, macro tunnel preservation, cross-kind and inherited collision checks, custom-event override separation, and reference-safe signature changes.
- Added public-API probes, exact Python schemas, native Automation/Undo/Redo/compile/save coverage, examples, and cross-process restart proof for persisted macro/custom-event shells.

## 0.6.0 — 2026-07-21

- Added bounded function, ordered parameter, and function-local-variable inspection with stable graph/local GUIDs, ownership/editability, complete signatures, metadata, required nodes, RepNotify relationships, and reference summaries.
- Extended `blueprint_member_edit` with transactional user-function shell/signature/metadata operations and function-local add/rename/type/default/remove operations while keeping the released ten-tool surface unchanged.
- Added reference/const K2 parameter forms, complete preflight validation, reject-only policies for referenced signatures/locals, and RepNotify coupling with exact notification signatures and live lifetime conditions.
- Added public-API probes, schemas, native Automation/Undo/Redo/compile/save coverage, documentation/examples, and cross-process restart proof for stable function/local identities and persisted RepNotify relationships.

## 0.5.0 — 2026-07-21

- Added bounded typed Blueprint member-variable inspection with stable GUID identity, ownership/editability, canonical K2 types, tagged defaults, validated metadata, replication/RepNotify relationships, and reference summaries.
- Added transactional `blueprint_member_edit` operations for add, identity-preserving rename, single-field update, and safe removal, with reject-only reference policies for type changes and deletion.
- Added a shared live K2 type/default codec covering supported scalar, container, enum, struct, and hard/soft object/class categories without accepting serialized engine structures.
- Added schema, bridge, native Automation, Undo/Redo, compile/save, and cross-process restart coverage proving typed members and defaults persist through the production bridge.

## 0.4.0 — 2026-07-21

- Added caller-generated operation IDs, process/bridge identity, canonical request digests, retained terminal replay, conflict detection, queued cancellation, expiry/bounds, `operation_status`, and explicit unknown-outcome recovery for every mutation.
- Added transactional `blueprint_component_edit` operations for local component add/remove/rename/reparent/root/property changes with stable identity, ownership, class, hierarchy, and snapshot validation.
- Added `blueprint_default_edit` plus targeted component/class-default inspection through one shared bounded reflected-property codec and reference policy.
- Added native ledger, component/default, stale-precondition, Undo/Redo, compile/save, and restart tests, including deliberate lost-response reconciliation and same-ID replay through the production bridge.

## 0.3.0 — 2026-07-21

- Added `blueprint_create` with native and Blueprint-generated Actor parent validation, strict no-overwrite semantics, `/Game` and local project-plugin confinement, symlink/path guards, mandatory initial compile/save, and deterministic unpublished-asset cleanup.
- Added explicit `blueprint_compile` with 64 bounded structured diagnostics and `blueprint_save` with non-interactive package saving and distinct write-conflict/save failures.
- Added read-back snapshots to every mutation result and cross-process proof that a Blueprint created through the authenticated Python bridge retains its parent, compiled state, and exact snapshot after editor restart.
- Added native coverage for invalid/skeleton parents, duplicate/case-only destinations, engine/external/local-plugin mounts, read-only paths, injected compile/save failures, cleanup-and-retry, and preservation of existing assets.

## 0.2.1 — 2026-07-21

- Expanded read-only Actor Blueprint discovery and exact inspection from `/Game` to every mounted content namespace available to the project, including `/Engine` and enabled plugin content.
- Published the durable asset-access split: reads cover all mounted content, while future mutations are confined to `/Game` and content plugins physically installed under the project's local `Plugins/` directory.
- Added native coverage using a dynamically registered plugin-style mount and updated cross-process capability checks and examples.

## 0.2.0 — 2026-07-21

- Added exact, bounded `/Game` Actor Blueprint discovery through the Asset Registry without loading discovery candidates.
- Added targeted read-only inspection for summary, parent, compile state, component hierarchy and changed defaults, variables, graphs, nodes, pins, and connections, including optional inherited Blueprint content.
- Added Unreal GUID identities, structural snapshots, 30-second opaque single-use cursors, exact graph filters, shallow defaults, page/scan/structure ceilings, and explicit unsupported-value records.
- Added native proof that inspection preserves dirty, compile, selection, and transaction state, plus behavioral coverage for wrong types, empty and oversized graphs, cursor expiry/staleness, undo/compile/save identity behavior, and fresh-editor reload equality.
- Added context-efficient inspection examples and synchronized Python/plugin release metadata at 0.2.0.

## 0.1.0 — 2026-07-21

- Added the dependency-free Python stdio MCP server with bounded framing, schema validation, discovery, authenticated HTTP calls, structured errors, timeouts, cancellation, and exact-version handling.
- Added the Unreal 5.8 editor plugin with fail-closed per-project credentials, loopback-only HTTP routing, bounded authenticated commands, Game-thread dispatch, discovery heartbeat, and clean route/state teardown.
- Added the read-only `capabilities` and `editor_state` tools; no mutation command is registered.
- Compiled public API probes for HTTPServer, transactions, Kismet and Blueprint utilities, Subobject Data, K2 schema/actions, compiler diagnostics, Asset Registry, and package saving.
- Added Python unit tests, Unreal Automation Tests, and a cross-process macOS acceptance test.
