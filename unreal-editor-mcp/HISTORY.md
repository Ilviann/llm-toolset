# Unreal Editor MCP history

## Unreleased

- Standardized Unreal Engine 5.8 development tooling on the `UE58` environment variable and moved the disposable integration project to `ue-test/ue58/`.
- Added an independent CommonUI companion checkbox to the Windows deployment helper, including fixed base-dependent packaging, project/Engine installation, and transactional multi-plugin handling without changing runtime or plugin versions.
- Decomposed Python packaging, Windows deployment, and headless integration support tools behind stable entrypoints, with shared Unreal-local primitives, typed deployment and Blueprint scenario state, focused domain modules, and unchanged runtime/plugin versions.

## 0.35.0 — 2026-08-09

- Added the independently versioned optional `UnrealMCPCommonUI` 0.1.0 editor companion with all CommonUI dependencies isolated from the base plugin and unchanged companion API v1/schema revision 1.
- Added bounded read-only `UCommonUserWidget` Widget Blueprint inspection through ordinary `blueprint_inspect`, with typed widget-input, activation, and reference records, local/inherited ownership, unresolved-safe identities, and one combined snapshot.
- Added exact read/mutation capabilities, dynamic Python section admission, focused native and production-socket fixtures, independent Win64 packaging, release contracts, and CommonUI user/architecture/type documentation. macOS verification remains preferred follow-up work.

## 0.34.0 — 2026-08-09

- Added deterministic bounded `layered_v1` placement for changed nodes in function, macro, custom-event, and native-event block replacement while retaining exact explicit-position plans.
- Added scratch-resolved live replay, SCC-aware layering, stable barycenter sweeps, comment and unrelated-node collision handling, untouched-graph fingerprints, published resource limits, Windows native/headless coverage, and layout architecture, contract, and user documentation. macOS native verification remains preferred follow-up work.

## 0.33.0 — 2026-08-09

- Extended `blueprint_block_replace` from complete user functions to local macros, custom-event handlers, and native-event-rooted handlers through one bounded scratch-compile, fingerprint, retained-operation, transaction, rollback, and preservation engine.
- Added exact inspected ownership boundaries and declared external crossing links, scratch-only identity rebinding for duplicated event/external nodes, uniform Python schemas, capabilities and limits, Windows native/headless coverage, examples, and complete user/architecture/type documentation. macOS native verification remains preferred follow-up work.

## 0.32.2 — 2026-08-09

- Fixed Blueprint inspection crashes when a Blueprint-owned reflected property, including a class-reference member, was exported or fingerprinted against a parent CDO that did not contain that field. Reflected property exports now use Unreal's archetype-size-aware default lookup across member, class, component, widget, and slot inspection.

## 0.32.1 — 2026-08-09

- Fixed companion startup ordering by making the base registry the sole loader of companion modules, preventing `UnrealMCPGAS` or the test fixture from calling the unloaded `UnrealMCP` module. Binary packaging and the Windows deployment helper now restore and verify source-owned descriptor contracts that Unreal AutomationTool omits. Advanced `UnrealMCPGAS` to 0.2.1 and the fixture to 0.1.1 without changing companion API v1.

## 0.32.0 — 2026-08-03

- Expanded the Windows graphical deployment helper with an optional `UnrealMCPGAS` companion build/install checkbox and exact project, Engine-enabled-by-default, and Engine-disabled-by-default installation modes.
- Added bounded atomic `.uproject` enablement, explicit installed-descriptor default state, and transactional base/companion replacement with rollback.

## 0.31.0 — 2026-08-03

- Added inspection-only `gameplay_effect` discovery to `UnrealMCPGAS` 0.2.0 and ordinary `blueprint_inspect` integration with eleven bounded typed sections covering duration/period, modifiers and magnitude forms, executions, stacking/overflow, cues, tags, granted abilities, additional effects, requirements, public Gameplay Effect Components, and cross-field relationships.
- Added explicit local/inherited values, compatible and unresolved class/attribute/tag/curve/asset references, stable nested identities, bounded cycle-aware chained-effect traversal, deterministic full-state fingerprints, and unchanged companion API v1/schema revision 1.
- Added exact read/mutation capabilities, dynamic Python catalog admission, read-only rejection checks, focused native and restart-persistent GAS fixtures, full production-socket inspection, independent companion versioning, and Gameplay Effect user/architecture/type documentation. The Windows deployment helper remains unchanged; macOS verification is tracked as preferred follow-up work.

## 0.30.0 — 2026-08-03

- Added the independently versioned optional `UnrealMCPGAS` 0.1.0 editor companion with all GAS dependencies isolated from the base plugin and exact companion API v1/schema admission.
- Added inspection-only `gameplay_ability` discovery and ordinary `blueprint_inspect` integration with typed policy, tag, trigger, and cost/cooldown reference records, inheritance sources, stable nested identities, truncation, and one combined non-mutating snapshot.
- Added separate read/mutation capabilities, dynamic Python section exposure, focused generic-family and GAS native Automation, headless registration checks, release/package contracts, offline GAS packaging, and complete user/architecture/type documentation. The Windows deployment helper remains unchanged; macOS verification is tracked as preferred follow-up work.

## 0.29.0 — 2026-08-03

- Added companion API v1 with bounded plugin discovery, exact descriptor/compiled admission, frozen base-owned registration, deterministic capabilities, three typed contribution categories, authenticated Game-thread dispatch, stale-safe transactional mutation, postcondition read-back, and shutdown ordering.
- Added exact Python/native schema intersection with readonly filtering and tool-list change notifications, plus an independently versioned disposable companion covering new asset, component, and existing-asset contributions.
- Added companion packaging support, author and user contracts, public-header probes, Python/server contracts, native authenticated-route Automation, and Windows production-socket acceptance. macOS verification remains preferred follow-up work.

## 0.28.0 — 2026-08-03

- Made readonly access the default nine-tool MCP catalog, required the explicit `--writable` trust decision for the 25-tool content-authoring catalog, and published the authoritative `access_mode` independently from native capabilities.
- Replaced `--tool-mode` and `--editor` with the single `--editor-lifecycle <absolute-executable>` opt-in, yielding independent readonly/writable and lifecycle configurations with exact 9/10/25/26 tool catalogs.
- Split retained-result lookup from cancellation: `operation_status` is readonly and lookup-only, while writable `operation_cancel` owns safe cancellation. Added access-before-dispatch contract coverage, native dirty/Undo preservation coverage, and production readonly content-fingerprint scenarios.
- Updated the Windows deployment helper to generate readonly LM Studio configuration by default and offer independent writable-access and selected-Engine lifecycle options.
- Passed real Windows lifecycle-only launch/restart/shutdown acceptance with bridge-instance replacement, two consecutive full headless cross-process runs, adaptive and forced-unity builds, 40 native Automation cases, and final Win64 package/deployment-helper qualification; advanced the exact-version pair to 0.28.0. macOS verification remains preferred follow-up work.

## 0.27.0 — 2026-08-03

- Made `capabilities` return the configured `.uproject` name/hash and Python metadata even when Unreal is inactive, with explicit `bridge_ready: false` and `native_capabilities_available: false` markers and no fabricated native fields.
- Preserved authentication, configuration, timeout, cancellation, version, and invalid-response failures; advanced the exact-version Python/plugin pair to 0.27.0.

## 0.26.0 — 2026-08-02

- Added ledger-backed `level_actor_edit` with exact current-map snapshots, full bounded-batch prevalidation, mixed native/Blueprint spawning, Actor/component metadata and property operations, attachment safety, non-World-Partition level moves, deletion, scoped World Partition loading, one editor transaction, verified rollback, and exact operation/package read-back.
- Added ledger-backed `level_save` with explicit current-world package ownership and writability preflight, per-package persistence evidence including verified external-package deletion, inspect/reload Actor and component verification, and honest retained partial outcomes without claiming cross-package filesystem atomicity.
- Added synchronized schemas, capabilities, limits, documentation, a mixed actor-placement workflow, Python contracts, focused Windows World Partition Automation, and production replay/stale/restart acceptance; advanced the exact-version pair to 0.26.0. macOS native verification remains in the platform backlog.

## 0.25.0 — 2026-08-01

- Added ledger-backed `level_manage` blank/template creation and exact-current-map configuration with explicit mounted World paths, current-map snapshots, topology facts, a 16-field World Settings allowlist, shared typed property encoding, save/reload read-back, and exact map identities.
- Extended `asset_delete` with inactive-map deletion: bounded complete root/build-data/external package closure enumeration, whole-closure registry/live reference and editor-state preflight, public Unreal reference-aware deletion, and per-package registry/storage verification without force or direct filesystem removal.
- Added synchronized schemas/capabilities/limits, a complete create-configure-delete example, Python contracts, focused non-partitioned and World Partition Windows Automation, and production cross-process/restart coverage; advanced the exact-version pair to 0.25.0. macOS native verification remains in the platform backlog.

## 0.24.0 — 2026-07-30

- Extended `level_inspect` with exact-snapshot actor pages, World Partition descriptor records, map-qualified Actor GUID identities, exact filters, and bounded single-use pagination without broad actor loading.
- Added exact actor/component inspection with stable component identities and origins, one-actor targeted loading, and requested-only reflected instance properties using the shared bounded value codec and stricter unsafe-property rejection.
- Added published limits, Python schemas and contracts, public Unreal API probes, macOS native Automation coverage, production cross-process paging/filter/property/restart verification, architecture/type documentation, and a complete World Partition workflow example. Windows native verification remains tracked in the platform backlog.

## 0.23.1 — 2026-07-30

- Verified every released feature on Apple Silicon macOS 26.5.2 with Unreal Engine 5.8 and Xcode 26.1.1 through 95 Python tests, adaptive and forced-unity editor builds, 37 native Automation cases, packaging validation, and the complete two-process production bridge workflow; cleared the macOS native platform backlog.
- Fixed rejected destructive user-defined-struct edits marking an otherwise clean package dirty by moving identity and dependency preflight ahead of the transaction and package modification.
- Fixed graceful editor shutdown crashing after Asset Registry teardown, corrected macOS lifecycle path expectations, made cross-process verification retry-safe and consistently graceful, completed the native-case manifest, and made overloaded Widget Tree API probing explicit for Clang and MSVC.

## 0.23.0 — 2026-07-29

- Added `blueprint_block_replace` for one complete user-owned function with exact inspected entry/result/body/local fingerprints, bounded action-backed semantic plans, explicit positions, typed defaults, and direct or explicitly positioned conversion connections.
- Added isolated non-transient scratch application and compilation before one live transaction, semantic scratch/live parity checks, exact rollback verification, retained replay/reconciliation, and function-boundary inspection records.
- Added Python contract tests, Windows native Automation coverage for success, Undo/Redo, stale and compile failures, live rollback, compile/save, plus production cross-process lost-response, replay, save, and restart verification.

## 0.22.1 — 2026-07-29

- Added an opt-in Windows deployment checkbox that installs matching `Binaries/Win64` PDB crash symbols beside plugin DLLs while continuing to exclude unrelated and intermediate debug artifacts.
- Added strict matching-symbol verification, rollback-compatible installation coverage, and updated deployment documentation. Symbol-free deployment remains the default.

## 0.22.0 — 2026-07-28

- Added stale-safe common panel-slot layout and bounded widget presentation edits through recursively typed reflected values, with live allowlists and inspection read-back.
- Added exact widget-property bindings with explicit polling cost, Designer-event graph binding/unbinding, stable graph/node identities, reference safety, and binding-aware snapshots.
- Split UMG authoring into focused layout, style, binding, and shared-support native components; added Python schemas, native Windows Automation coverage, documentation, and a complete workflow example.

## 0.21.1 — 2026-07-28

- Split native Game Data and Blueprint Graph Editor code into focused request validation, operation-handler, and result/inspection-builder translation units without changing MCP schemas or behavior.

## 0.21.0 — 2026-07-28

- Added the `widget` Blueprint family, specialized Widget Blueprint creation, bounded tree/default inspection, stable widget and slot identities, and explicit rejection of Actor component operations.
- Added stale-safe `widget_tree_edit` operations for roots, panel/named-slot children, removal, rename, reparenting, variable exposure, and supported widget defaults, with transactions, reference checks, cycle/root protection, replay, and published limits.
- Added Python schemas, public UE 5.8 API probes, focused native Automation coverage, Windows build/test verification, cross-process restart read-back, documentation, and a workflow example.

## 0.20.0 — 2026-07-28

- Added ledger-backed `asset_delete` for one exact persisted project or local-project-plugin asset package after stale-safe reference, editor-state, mount, package, storage, and Unreal deletion-reference preflight.
- Added public non-force `ObjectTools` deletion/cleanup with registry and storage verification, explicit non-Undo semantics, and retained `partial` outcomes when persistence evidence disagrees.
- Added Python schema/contract coverage, public API probes, focused native Automation coverage, cross-process deletion/restart verification, documentation, and a bounded workflow example.

## 0.19.0 — 2026-07-28

- Added read-only `asset_references` for exact mounted assets, with separate serialized, management, searchable-name, open-editor, and direct loaded-object evidence.
- Added deterministic exact snapshots, bounded pages, single-use cursors, Asset Registry stale-cursor rejection, explicit completeness/limitation fields, and published reference-scan limits.
- Added Unreal 5.8 public-API probes, a 32nd native automation case, Python schema/contract coverage, and cross-process serialized-reference verification.

## 0.18.0 — 2026-07-27

- Added bounded `level_inspect` discovery plus exact current-map identity, revision, snapshot, dirtiness, World Partition, and external-actor state.
- Added ledger-backed `level_open` for exact mounted World assets with dirty/editor-busy refusal, no implicit save or discard, and exact post-open verification.
- Added public Unreal API probes, a 31st native automation case, and cross-process lost-response, restart-stability, and exact-map verification on Windows.

## 0.17.1 — 2026-07-27

- Added a Windows tkinter deployment helper that selects an existing Unreal project, discovers or validates its Engine installation, builds an installed Win64 plugin with the local Visual Studio toolchain, strips implementation source and external debug artifacts while retaining module rules and precompiled metadata, and installs it with replace-safe staging.
- The deployment GUI initializes its Engine selection from the configured Engine environment path and preserves a valid value when the project is selected.
- Added one-click LM Studio JSON generation for the selected checkout and `.uproject`, plus focused offline tests and deployment architecture/contracts.
- Fixed first-reload Blueprint snapshot instability by excluding only UE 5.8's hidden, untyped, unlinked, default-free `ErrorTolerance` pin on promotable operators from model-facing pin identities and structural fingerprints; typed or otherwise meaningful tolerance pins remain structural.
- Added native regression coverage for the regenerated tolerance-pin GUID and verified the exact production-created Blueprint snapshot across the Windows cross-process restart workflow.

## 0.17.0 — 2026-07-27

- Added opt-in large-mode `editor_lifecycle` with configured macOS/Windows launch, exact authenticated readiness, idempotent already-running detection, cancellation, and bounded startup/abnormal-exit outcomes.
- Added bridge-owned graceful shutdown that refuses PIE/simulation, saving, garbage collection, transactions, asset compilation, and dirty packages; no model argument can select a process or request forced termination.
- Added restart composition across graceful shutdown, process exit, detached relaunch, rediscovery, reauthentication, exact-version matching, and old/new bridge-instance validation.
- Added bounded atomic lifecycle records, stale/interrupted recovery, focused Python/native protocol coverage, lifecycle architecture/contracts, and user configuration/recovery guidance.

## 0.16.0 — 2026-07-23

- Added `game_data_inspect` and reconciled `game_data_edit` for bounded user-defined struct schemas and typed Data Tables, bringing the released surface to fifteen exact tools.
- Added stable struct-member identities, safe schema evolution, bounded dependency rejection for destructive changes, typed row creation and inspection, and transactional add/replace/rename/remove/batch row authoring.
- Added one reflected row-value codec for scalars, enums, common/native/user structs, arrays, sets, maps, and compatible references with strict depth, field, collection, row, dependency, mutation-scope, and unsupported-property limits.
- Added package saving, snapshot/cursor/replay contracts, explicit failure restoration, Python schema coverage, native Phase 17 Automation coverage, public API probes, architecture/type documentation, and a project-neutral data-table workflow example.
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
