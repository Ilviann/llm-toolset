# Remaining Unreal Editor MCP Roadmap

This roadmap contains only the remaining implementation work. The target is Unreal Engine 5.8 and newer. macOS remains the primary development host. Native Windows validation begins with the Actor beta and becomes a release gate for every later feature phase. Linux remains source-portable with unit-tested platform branches until a native host is available.

## Top-level checklist

- [ ] Phase 4 — Add reliable mutation delivery, Actor component editing, and Blueprint/component defaults.
- [ ] Phase 5 — Add Blueprint members, signatures, logic-container shells, and matching inspection.
- [ ] Phase 6 — Add bounded, read-only discovery of context-valid Blueprint actions.
- [ ] Phase 7 — Add complete atomic graph editing, including node creation and wiring.
- [ ] Phase 8 — Harden and natively validate the complete Actor Blueprint beta workflow.
- [ ] Phase 9 — Formalize GameMode and GameState support and add GameInstance support.
- [ ] Phase 10 — Add transactional replacement of one complete bounded Blueprint logic unit.
- [ ] Phase 11 — Complete cross-platform qualification and publish the first stable-tagged release.
- [ ] Phase 12 — Add optional editor launch, restart, and graceful shutdown workflows.
- [ ] Phase 13 — Add optional editor-offline C++ project-generation and build workflows.

Keep this checklist synchronized one-for-one with the phase sections below. Complete phases in order unless a phase explicitly permits parallel platform validation. Every completed phase must include implementation, tests, documentation, examples, and a releasable version update.

## Architectural baseline for remaining work

### Process boundary

The application remains an exact-version pair:

1. A dependency-free Python 3.10+ MCP server using stdio JSON-RPC.
2. An Unreal Editor C++ plugin using public editor APIs and a bounded authenticated localhost HTTP bridge.

Python owns MCP framing, published schemas, exact argument validation, tool modes, project configuration, discovery, authenticated HTTP calls, timeouts, process orchestration, and stable error presentation. The C++ plugin owns credentials, listener lifecycle, authentication, Game-thread dispatch, Unreal object access, Blueprint operations, compiler diagnostics, transactions, package saving, mutation-result retention, and authoritative capabilities.

Deploy the Python package and C++ plugin as an exact-version pair. Report both versions and the installed Unreal version through `capabilities`; reject a mismatched pair before mutation. Support for later Unreal releases must be proven through compilation and behavioral tests, not inferred from version numbers.

### Remaining model-facing tool surface

Keep the public surface compact. Add typed operations to these remaining tool families rather than publishing a separate tool for every native handler:

| Tool | First phase | Responsibility |
| --- | ---: | --- |
| `operation_status` | 4 | Resolve the retained outcome of one mutation operation without executing it again |
| `blueprint_component_edit` | 4 | Perform one typed component-hierarchy or component-default mutation |
| `blueprint_default_edit` | 4 | Set one supported Blueprint class-default property |
| `blueprint_member_edit` | 5 | Perform one typed variable, function, macro, or custom-event mutation |
| `blueprint_action_catalog` | 6 | Discover a bounded set of context-valid graph actions without mutation |
| `blueprint_graph_edit` | 7 | Perform one typed node, pin, connection, position, or removal mutation |
| `blueprint_block_replace` | 10 | Replace one complete bounded logic unit as a prevalidated transaction |
| `editor_lifecycle` | 12 | Run one opt-in configured launch, restart, or graceful-shutdown operation |
| `project_build` | 13 | Run one opt-in configured project-generation or editor-target build operation |

Lifecycle and build tools remain absent from the default model context. Use an opt-in large mode for them. Measure the Blueprint schemas and use nested operation discriminators if context cost becomes excessive; the default mode must still support the complete Blueprint-authoring workflow.

### Mutation delivery and concurrency contracts

- Require a caller-generated `operation_id` for every mutating call, including existing mutation tools. Bind it to the exact normalized arguments, project identity, bridge instance, and authenticated client context.
- Retain a bounded operation ledger with published capacity and lifetime limits. Repeating an operation ID with the same request returns the retained result; reusing it with different arguments returns a stable conflict and never executes.
- Publish explicit operation states such as `queued`, `executing`, `committed`, `rejected`, and `outcome_unknown`. Never report cancellation after a mutation has committed.
- Cancellation may remove queued work or stop preflight work, but it must not interrupt an active Unreal mutation at an unsafe point. A lost response must be reconciled through `operation_status` before retry.
- The ledger is process-scoped unless a later operation explicitly defines durable restart state. If the bridge instance changes and no result is available, return `outcome_unknown` and require inspection before further mutation.
- Reject mutation while the target Blueprint is compiling, saving, loading, being reinstanced, undergoing undo/redo, or otherwise unable to provide stable preconditions.
- Use one editor transaction per accepted atomic mutation. Prevalidate before opening it, verify postconditions before commit, and implement explicit restoration for unexpected failure. Do not assume that cancelling a transaction restores arbitrary object state.

### Blueprint identity, type, and property contracts

- Use Unreal long package names and object/class paths at the model boundary; reject raw filesystem paths and traversal.
- Read-only operations may inspect any content mount visible to the project. The Blueprint being mutated must remain confined to `/Game` or a content mount owned by a plugin physically inside the current project's local `Plugins/` directory.
- A referenced class or asset is not itself a mutation target. Permit type-compatible native classes and packageable assets from visible mounted content while rejecting transient, editor-only, unresolved, incompatible, or unsafe references.
- Give components, members, graphs, nodes, pins, inspection snapshots, and bridge instances explicit identities. Mutation targets must have stable identities; an unavailable identity is not silently replaced by a name lookup.
- Require the current structural snapshot and all relevant object identities for mutation. Return `stale_precondition` instead of retargeting reconstructed or replacement objects.
- Reuse one bounded canonical K2 type and reflected-property codec across inspection and mutation. Add a type or value form only with read/write round-trip tests and explicit unsupported behavior.
- Validate every MCP argument against its published schema in Python and again against the live Unreal object, graph schema, property metadata, and family capabilities in C++.
- Use one stable bounded error envelope with `code`, `message`, `details`, and `retryable`. Never return C++ exceptions, assertions, addresses, credentials, or unbounded logs.
- Keep request bodies, JSON depth, strings, collections, scans, caches, operation state, diagnostics, response bytes, transaction work, and Game-thread time explicitly bounded and published through `capabilities`.

### Security baseline

- Bind only to `127.0.0.1` and verify the actual listening address in native integration tests.
- Authenticate every request with the high-entropy per-project credential and fail closed on credential, listener, route, or heartbeat faults.
- Never expose the credential or absolute project path through discovery, capabilities, operation records, diagnostics, or logs.
- Permit one bridge owner per configured port; bound queued requests and retained state; and cleanly release route, discovery, credentials, and pending work during shutdown.
- Never expose arbitrary UObject calls, unrestricted reflection mutation, Python execution inside Unreal, console commands, supplied C++, arbitrary subprocess arguments, or general filesystem/process access.

### Release discipline

Increment the minor version for each remaining feature phase and the patch version for fixes. A major-version promotion requires a separate explicit decision. Keep Python metadata, `.uplugin` metadata, runtime capabilities, tests, README, examples, and `HISTORY.md` synchronized after every phase.

## Phase 4 — Reliable mutations, Actor components, and defaults

**Outcome:** Agents can reliably reconcile mutation outcomes, build an Actor component hierarchy, and edit supported component and Blueprint class defaults without duplicate execution or ambiguous timeouts.

### Implementation

- Add the process-scoped mutation ledger, caller-generated operation IDs, request digests, bridge-instance identity, retained-result replay, `operation_status`, bounded eviction, and explicit cancellation/unknown-outcome semantics described in the baseline. Apply the contract to every mutating tool.
- Add `blueprint_component_edit` operations for one add, remove, rename, reparent, root change, or component-default property change.
- Use the public Subobject Data Subsystem and Simple Construction Script APIs only after native behavioral probes prove add, delete, rename, reparent, root, notification, identity, and undo behavior in Unreal 5.8.
- Distinguish inherited, native, instanced, and locally owned components. Permit mutation only where ownership and the live Blueprint component capability allow it.
- Accept only suitable `UActorComponent`-derived classes. Reject abstract, deprecated, editor-only, transient, incompatible, non-spawnable, or otherwise invalid component classes before mutation.
- Enforce unique names, legal attachments, scene/non-scene rules, root invariants, ownership, stable component identity, and current-snapshot preconditions.
- Add targeted inspection of editable component and Blueprint class-default properties. Avoid dumping every reflected property.
- Add `blueprint_default_edit` for one supported editable property on the Blueprint-generated class default object. Use the same codec and property policy as component defaults.
- Begin the shared property codec with booleans, finite numbers, strings/names/text, enums and flags, common math/color/transform structs, native and Blueprint class references, and visible packageable asset references. Reject delegates, raw pointers, arbitrary object graphs, unsupported containers, transient/editor-only objects, and properties without safe editable semantics.
- Return the retained operation outcome, new snapshot, concise changed record, dirty state, and any identities reconstructed by the edit. Compilation and saving remain explicit.

### Verification

- Test lost responses, same-ID replay, conflicting ID reuse, ledger bounds and expiry, queued cancellation, commit-before-disconnect, bridge restart, unknown outcomes, stale snapshots, and inspect-before-retry recovery.
- Test scene and non-scene components, root creation and replacement, attachment cycles, duplicate names, invalid component classes, inherited/native component rejection, reparenting, rename, and removal.
- Test every supported property form on component templates and the Blueprint class default object, including native classes and visible engine/project/plugin asset references, incompatible references, unsupported values, and exact read/write round trips.
- Test undo/redo, compile, save, reload, and identity behavior. Prove through structural and dirty-state comparison that rejection and unexpected failure leave no partial mutation or misleading transaction record.

### Documentation and release gate

- Document operation reconciliation, component identities and ownership, root and attachment rules, supported property forms, reference policy, explicit compile/save workflow, and Undo recovery.
- Release `0.4.0` only after MCP calls create and persist a multi-component Actor Blueprint, edit an Actor class default, and safely reconcile a deliberately lost mutation response.

## Phase 5 — Blueprint members, signatures, and logic containers

**Outcome:** Agents can inspect and define the typed data, signatures, and named logic containers needed by Actor Blueprint code.

### Implementation

- Extend `blueprint_inspect` with bounded member, function/macro/custom-event signature, parameter, local-variable, metadata, ownership, and reference-summary records. Return stable identities and exact graph relationships needed for mutation preflight.
- Add `blueprint_member_edit` operations for one add, rename, supported update, or safe removal of a member variable, local variable, function, macro, or custom-event shell.
- Reuse the canonical K2 type/value codec. Define explicit scalar, container, object/class reference, const/reference, and local-scope forms rather than accepting serialized engine structures.
- Validate names, inheritance collisions, type compatibility, defaults, replication and RepNotify relationships, instance editability, visibility, categories, function access, pure/const flags, inputs, outputs, and local scope through live Blueprint capabilities.
- Create function and macro graphs through public Blueprint utilities; preserve required entry/result/tunnel nodes and validate complete signatures before mutation.
- Create custom events only in compatible event graphs. Keep override events, interface functions, inherited functions, and user-owned members distinct.
- Use `reject_if_referenced` as the only removal/signature-change policy in this phase. Do not cascade-delete nodes, silently orphan references, or attempt automatic graph repair.
- Return the operation result, concise member/graph identities, reference summary, reconstructed identities, and new Blueprint snapshot.

### Verification

- Test every supported type, parameter direction, metadata flag, and member kind; invalid names; inherited and cross-kind collisions; local-variable scope; function/macro signatures; custom-event restrictions; and unsupported Blueprint capabilities.
- Test member and local-variable references, RepNotify coupling, used functions, required graph nodes, safe removal, rejected referenced removal, undo/redo, compilation, saving, restart, and read-back.
- Prove that inspection exposes enough information to plan every accepted mutation and that rejected mutations preserve structure, dirty state, compile state, and transaction history.

### Documentation and release gate

- Document the shared type vocabulary, metadata and signature rules, reference policy, function/macro/event distinctions, and inspect-before-edit examples.
- Release `0.5.0` when variables, signatures, local variables, and empty logic containers survive compile, save, restart, and exact bounded inspection.

## Phase 6 — Context-valid action discovery

**Outcome:** Agents can discover a small relevant set of actions valid for one graph without guessing node classes or mutating the Blueprint.

### Implementation

- Add read-only `blueprint_action_catalog` with exact text, owner class, function/member, node family, graph, and optional pin-context filters.
- Build results from Unreal's public Blueprint action/spawner and live schema filtering APIs. Do not expose unrestricted node-class construction or accept forged action signatures.
- Initially cover event, function-call, variable get/set, flow-control, cast, literal, and common operator families that pass the live graph and family filters.
- Return opaque action IDs bound to the bridge instance, target Blueprint class, graph schema and identity, structural snapshot, normalized query, and rebuildable action signature.
- Bound action-database scans, elapsed time, result count, encoded bytes, retained action records, cache lifetime, and concurrent catalog work. Publish the effective limits.
- Keep this phase mutation-free. Node creation moves to Phase 7 so no release depends on manual editor wiring to complete an MCP-created graph change.

### Verification

- Test pure and impure functions, static and instance context, inherited and local members, unique events, latent calls, incompatible graph types, pin context, family restrictions, narrow filters, truncation, timeout, cache eviction, expiry, bridge restart, and stale snapshots.
- Confirm that forged names, class paths, signatures, expired IDs, cross-project IDs, and IDs from another graph or snapshot cannot invoke or resolve unsupported/internal actions.
- Prove catalog calls do not dirty, transact, compile, save, select, or otherwise mutate the Blueprint.

### Documentation and release gate

- Document the inspect → narrow catalog workflow, action identity lifetime, filters, limits, and invalidation rules.
- Release `0.6.0` when representative Actor event, function, and macro graphs return small context-valid catalogs with stable bounded behavior.

## Phase 7 — Complete atomic graph editing

**Outcome:** Agents can implement Actor Blueprint logic through small, individually prevalidated and reconcilable graph mutations.

### Implementation

- Add `blueprint_graph_edit` operations for `add_node`, `set_pin_default`, `connect_pins`, `disconnect_pins`, `remove_node`, and `move_node`. Add rename/comment operations only where public semantics and preservation behavior are proven.
- Require a valid retained action ID for `add_node`. Re-resolve and re-filter its rebuildable signature against the live graph before invoking the spawner; do not trust cached UObject pointers.
- Use the live K2 graph schema for pin compatibility, connection responses, default parsing, wildcard specialization, link replacement, link breaking, conversion policy, and safe deletion. Never write link arrays or unvalidated default strings directly.
- Default automatic conversion off. If explicitly enabled, bound inserted conversion nodes, include them in preflight and the transaction, and return all inserted node and pin identities.
- Require operation ID, Blueprint snapshot, graph identity, node identity, and pin identity preconditions as applicable. Reject read-only, inherited, interface, intermediate, construction, signature, or other protected graph/node targets unless the specific operation is proven safe.
- Assign persistent identities to created nodes and pins, detect spawners that return an existing unique node, and return a concise complete change record after any reconstruction.
- Prevalidate the complete action before opening one transaction. Verify links, defaults, positions, ownership, structure limits, dirty state, and expected reconstruction afterward; restore explicitly on unexpected failure.
- Bound graph size, links per pin, position, default size, inserted nodes, result size, transaction work, diagnostics, retained operation state, and Game-thread duration.

### Verification

- Test event, function, macro, pure and impure nodes; unique-event spawners; variable nodes; execution and data pins; compatible and incompatible types; wildcard specialization; defaults; and object/class/asset references.
- Test conversion disabled/enabled, duplicate and replacement links, disconnect, protected-node deletion, move bounds, stale identities, spawner failure, returned-existing-node behavior, undo/redo, compile, save, restart, and reload.
- Test lost-response reconciliation for every operation family. Capture the graph before every rejection and injected failure and prove equality of structure, dirty state, compile state, and transaction history afterward.

### Documentation and release gate

- Add complete atomic graph-editing recipes, conversion policy, operation reconciliation, and guidance for re-inspection after reconstruction or compilation.
- Release `0.7.0` when MCP calls alone can implement, compile, save, restart, and verify a small BeginPlay-driven Actor behavior using components and variables.

## Phase 8 — Actor workflow hardening and native beta

**Outcome:** The complete create/open → inspect → component/default/member/graph edit → compile → save → inspect workflow is dependable for supervised Actor Blueprint development on macOS and has passed its first native Windows qualification.

### Implementation

- Normalize compiler warnings, errors, notes, node and graph references, and source locations into bounded structured diagnostics with bounded continuation where needed.
- Audit every mutation for exact prevalidation, operation reconciliation, transaction scope, postconditions, package dirty state, rollback behavior, and deterministic handling while the editor is compiling, saving, loading, undoing, garbage collecting, playing, or shutting down.
- Measure tool-schema and representative-result token costs. Tighten descriptions, defaults, filters, result pages, and modes for small local models without hiding required identity, safety, or operation fields.
- Add deterministic seeded schema/value fuzz cases without third-party runtime dependencies.
- Build the plugin and run the complete Actor acceptance workflow on native Windows. Fix only source-evidenced platform differences behind narrow compatibility or platform adapters with tests.
- Maintain the canonical end-to-end fixture builder and behavioral integration suite in the disposable test project. Expected results must come from runtime contracts and created assets, not prose files or committed generated assets.

### Verification

- Run repeated sessions against clean and already-edited Actor Blueprints, including malformed requests, stale snapshots, lost responses, cancellation, timeout, compiler failure, save conflict, editor restart, bridge restart, and undo/redo.
- Run bounded native JSON/property decoder tests, operation-ledger stress tests, complete Python tests, Unreal Automation Tests, and cross-process workflows.
- On native Windows, verify credentials and permissions, loopback binding, discovery, paths and casing, plugin loading, Game-thread dispatch, component/member/graph transactions, compilation, saving, and exact model-facing contracts.
- Record peak request/response bytes, retained state, startup time, schema size, and typical operation latency on the 16 GB macOS development machine and the Windows qualification host.

### Documentation and release gate

- Complete the Actor-focused README, troubleshooting, security model, limits, mode guidance, LM Studio setup, operation recovery, Windows setup, and end-to-end tutorial.
- Release `0.8.0` as the Actor Blueprint beta only when the documented clean-project and existing-Blueprint workflows pass natively on macOS and the defined Windows beta suite passes.

## Phase 9 — Gameplay-framework Blueprint families

**Outcome:** The established workflow formally supports GameMode and GameState families and expands safely to GameInstance-derived Blueprints.

### Implementation

- Replace Actor-only eligibility checks in discovery, inspection, creation, compile, save, and mutation resolution with an explicit published family policy.
- Formally classify and qualify `AGameModeBase`/`AGameMode` and `AGameStateBase`/`AGameState` within the existing Actor-derived path rather than treating them as a new inheritance category.
- Add the UObject-based `UGameInstance` family without weakening Actor restrictions or assuming component support.
- Evaluate live Blueprint capabilities for defaults, components, event graphs, local variables, overrides, and graph types. Publish a family/operation capability matrix and reject unsupported operations before mutation.
- Reuse inspection, class defaults, members, action catalog, graph editing, compile, save, diagnostics, operation reconciliation, and security contracts.
- Add family-specific default properties, callbacks, override functions, and graph-action coverage. Keep every output family-aware.

### Verification

- Create, inspect, edit defaults and logic, compile, save, restart, and read back representative GameModeBase, GameMode, GameStateBase, GameState, and GameInstance Blueprints.
- Test framework callbacks, inherited functions, class defaults, supported and unsupported component operations, local-variable and graph capabilities, parent changes outside scope, and manual project-settings assignment of saved classes.
- Run the complete shared and family-specific suites natively on macOS and Windows and require identical normal model-facing contracts.

### Documentation and release gate

- Document the family capability matrix, default-property use cases, callbacks, component differences, and focused examples. Do not add project-settings mutation.
- Release `0.9.0` only when every supported family passes the shared contract and its family-specific restrictions on both native platforms.

## Phase 10 — Bounded logic-unit replacement and layout

**Outcome:** Agents can replace one complete supported event implementation, user-owned function, macro, or custom-event handler as a single prevalidated operation while preserving unrelated Blueprint content.

### Implementation

- Add `blueprint_block_replace` using the same member, action-catalog, type/value, graph-edit, operation-ledger, and diagnostic primitives rather than a second mutation engine.
- Limit the first replacement contract to complete logic units with explicit ownership: one user-owned function, macro, custom-event handler, or one event-rooted implementation with bounded declared external links. Arbitrary selected graph regions remain deferred.
- Define required entry/exit identities, owned nodes, allowed external data/control links, local variables, replacement operations, action signatures, limits, expected fingerprints, and current-snapshot preconditions.
- Preflight without supplied code or free-form Blueprint text. Use an isolated non-transient scratch Blueprint/package or a semantic preflight proven behaviorally equivalent to live spawning; do not assume transient-graph node spawning matches a live graph.
- Compile the isolated candidate when supported, compare the planned postconditions, and remove all scratch objects and registrations before touching the live Blueprint.
- Apply the validated live replacement as one reconciled editor transaction. Preserve unrelated graphs, nodes, variables, metadata, links, positions, bookmarks, comments, and prior dirty state; explicitly restore on any unexpected result.
- Add a deterministic bounded layout algorithm for changed nodes only. Preserve untouched positions and handle execution flow, data dependencies, cycles, comments, graph bounds, and inserted conversion nodes predictably.

### Verification

- Replace representative event implementations, functions, macros, and custom-event handlers with internal and declared external links. Test invalid boundaries, cycles, latent nodes, locals, stale snapshots, expired actions, compile failure, timeout, lost response, rollback, undo/redo, save/reload, and unchanged-content fingerprints.
- Prove scratch preflight/live parity for every supported node family. Prove failed preflight creates no live transaction and unexpected live failure restores exact inspected structure and prior dirty state.
- Run the complete replacement and preservation suites natively on macOS and Windows.

### Documentation and release gate

- Document logic-unit ownership, boundary links, scratch preflight, layout, preservation guarantees, limits, operation recovery, and when atomic actions remain preferable.
- Release `0.10.0` only when unrelated-content fingerprints remain stable across successful, rejected, timed-out, and replayed replacements on both native platforms.

## Phase 11 — Cross-platform qualification and stable release

**Outcome:** The complete supported Blueprint-authoring feature set is packaged, documented, and release-qualified on native macOS and Windows without depending on optional lifecycle or build tools.

### Implementation

- Build and package the exact Python/plugin pair on native macOS and Windows with Unreal 5.8. Add only source-evidenced compatibility fixes behind narrow platform or Unreal-version adapters.
- Run the complete Python, Unreal Automation, cross-process bridge, Actor, framework-family, block-replacement, restart-readback, operation-reconciliation, and preservation suites on both platforms.
- Re-run the security audit: credential faults, bad-token isolation, loopback-only listening, discovery secrecy, request bounds, timeouts, duplicate ownership, operation-ledger isolation, and shutdown cleanup.
- Require identical published schemas, stable errors, limits, versions, operation semantics, family capabilities, and core Blueprint results. Keep filesystem and process differences out of normal contracts.
- Produce offline-installable artifacts and verify a clean-machine installation without accounts, cloud services, telemetry, network downloads, or generated test fixtures.
- Record exact platform, Unreal patch, compiler/toolchain, package format, performance/context measurements, native results, and known limitations.

### Verification

- Run clean-project and existing-project acceptance workflows from packaged artifacts on both platforms.
- Verify metadata, runtime capabilities, README examples, history, package contents, licenses, generated-state exclusions, and exact Python/plugin version matching from executable contracts.
- Keep Linux portability branches unit tested and documented; do not claim native Linux qualification.

### Documentation and release gate

- Publish complete installation, upgrade, offline preparation, troubleshooting, security, recovery, compatibility, and known-limitations documentation.
- Release `0.11.0` as the first stable-tagged release only when both native platform suites and both clean-machine offline installations pass. A later major-version promotion remains a separate explicit decision.

## Phase 12 — Optional editor lifecycle workflows

**Outcome:** Agents can opt in to launching, restarting, and gracefully shutting down only the configured Unreal project/editor instance.

### Implementation

- Add the single `editor_lifecycle` tool only in opt-in large mode, with typed `launch`, `restart`, and `shutdown` operations. Keep normal state reporting in the existing editor-state surface.
- Accept no executable path, project path, environment variable, or arbitrary process argument from the model. Configure and validate absolute editor and `.uproject` paths at MCP startup and expose only bounded availability information.
- Launch one detached configured editor instance through platform adapters. Detect the exact project-specific authenticated bridge and distinguish `starting`, `ready`, `already_running`, cancelled, timed out, and failed startup.
- Implement graceful bridge-owned shutdown with bounded dirty-package summaries and explicit refusal while unsafe editor work is active. Do not provide forced process termination.
- Implement restart as a durable lifecycle operation containing exact project identity, Python/plugin version, bridge instance, and operation identity. Reconcile disconnect, rediscovery, reauthentication, version matching, and final readiness.
- Keep lifecycle operation retention separate from the process-scoped Blueprint mutation ledger and clean stale durable records safely.

### Verification

- Test missing and malformed configuration, paths with spaces, another project or process on the port, repeated launches, version mismatch, dirty packages, active compilation/save/PIE, graceful shutdown, restart success, stale durable records, timeout, cancellation, and abnormal termination.
- Run launch, graceful shutdown, restart, reconnection, and recovery natively on macOS and Windows. Unit test Linux command construction without claiming native support.
- Prove the model cannot substitute executables, projects, environment values, shell fragments, or arbitrary arguments.

### Documentation and release gate

- Document opt-in configuration, platform paths, dirty-content policy, durable restart semantics, cancellation, recovery, and default-mode exclusion.
- Release `0.12.0` only when configured lifecycle workflows complete without arbitrary process execution or data loss on native macOS and Windows.

## Phase 13 — Optional editor-offline C++ project orchestration

**Outcome:** Agents can opt in to narrowly configured project-file generation and editor-target builds only while the configured project editor is stopped.

### Implementation

- Keep C++ source editing outside this application. Pair with `rooted-files-mcp` for separately configured confined text edits.
- Add the single `project_build` tool in opt-in large mode with typed `generate_project_files` and `build_editor_target` operations.
- Resolve Unreal Build Tool or platform scripts from validated startup configuration and the installed engine layout. Use fixed templates owned by platform adapters.
- Let the model select only targets and configurations from a bounded published allowlist. Never accept executable paths, project paths, shell fragments, environment variables, compiler/linker flags, or arbitrary arguments from a tool call.
- Refuse build work while the authenticated configured editor is running or its lifecycle state is uncertain. Reconcile with durable lifecycle operations before starting.
- Bound process count, queueing, duration, output capture, diagnostic count and size, retained operation results, cancellation escalation, and child-process cleanup.
- Normalize compiler diagnostics and keep subprocess output off MCP stdout except inside valid bounded tool results.

### Verification

- Test fixed command construction, allowlists, paths with spaces, missing tools, invalid targets/configurations, editor-running and uncertain-state rejection, timeout, cancellation, nonzero exit, oversized logs, retained-result replay, and process-tree cleanup.
- Run native offline project generation and editor-target builds from packaged configuration on macOS and Windows without network access or runtime downloads. Unit test Linux construction separately.
- Prove that tool arguments cannot alter the executable, project, environment, working directory, command template, or unrestricted flags.

### Documentation and release gate

- Document offline engine/tool preparation, configured allowlists, lifecycle interaction, bounded diagnostics, cancellation, platform behavior, and use with the confined file MCP.
- Release `0.13.0` only when both fixed native platform workflows are reproducible from clean documented configuration.

## Deferred and excluded scope

The following are not part of the committed remaining roadmap unless separately authorized:

- Arbitrary selected-region block replacement beyond the complete logic-unit boundaries supported in Phase 10.
- General filesystem access or C++ source modification.
- Arbitrary shell commands, compiler arguments, console commands, UObject calls, unrestricted reflection mutation, expressions, or supplied-code evaluation.
- Unrestricted whole-Blueprint text import/export or wholesale Blueprint replacement.
- Blueprint reparenting, project-settings mutation, timelines, event dispatchers, interface authoring, and specialized asset families unless added through a later roadmap update.
- Level Blueprint, Widget Blueprint, Animation Blueprint, Control Rig, Niagara, Material, Behavior Tree, or StateTree authoring.
- Play-in-Editor input injection, screenshots, runtime object mutation, or automated gameplay assertions.
- Cloud services, accounts, telemetry, dependency downloads, or a game-side network listener.

## Primary Unreal 5.8 API references

These references establish feasibility only. Each owning phase must add compiled public-header probes and behavioral tests before freezing its model-facing contract:

- [FScopedTransaction](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FScopedTransaction)
- [FAssetRegistryModule](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/AssetRegistry/FAssetRegistryModule)
- [FKismetEditorUtilities](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FKismetEditorUtilities)
- [FBlueprintEditorUtils](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FBlueprintEditorUtils)
- [USubobjectDataSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/SubobjectDataInterface/USubobjectDataSubsystem)
- [UBlueprintNodeSpawner](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintGraph/UBlueprintNodeSpawner)
- [UBlueprintNodeSpawner::Invoke](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintGraph/UBlueprintNodeSpawner/Invoke)
- [UEdGraphSchema_K2](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintGraph/UEdGraphSchema_K2)
- [FCompilerResultsLog](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FCompilerResultsLog)
- [UPackage saving](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/CoreUObject/UPackage)
