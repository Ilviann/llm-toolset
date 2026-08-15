# Feature documentation

This directory contains the detailed implementation, verification, documentation, and completion requirements for current and completed roadmap features. [`ROADMAP.md`](../../ROADMAP.md) remains the concise active feature checklist.

## Status groups

- [Planned features](planned/index.md).
- [Active features](active/index.md).
- [Completed features](completed/index.md).
- Deferred: None.

## Front matter contract

Every feature document begins with YAML front matter containing string `feature_id`, enum `status`, string-list `depends_on`, and nullable string `released_in`. The status must match its containing status directory. Runtime features omit `release_track` or set it to `runtime`; completed runtime features require a release version, while unreleased runtime features use `null`. Features that change only repository support tooling set `release_track: support-tooling` and always use `released_in: null`, including after completion. Dependencies name stable feature IDs or explicit issue IDs and must match the document's direct-prerequisite section.

## Roadmap workflow

The target is Unreal Engine 5.8 and newer. Windows support and applicable native verification are mandatory before feature completion and release. macOS support is preferred but non-blocking; completed features that still need macOS verification remain in the [`ROADMAP.md` native platform test backlog](../../ROADMAP.md#native-platform-test-backlog). Linux is outside the current support and verification scope.

Keep the authoritative checklist in [`ROADMAP.md`](../../ROADMAP.md) synchronized with every unfinished runtime feature and each completed runtime feature that is its direct prerequisite. Keep support-tooling features in the roadmap's separate **Support tooling** section even after completion. Feature identifiers are stable names rather than execution indexes. A feature may be implemented and completed whenever every direct dependency listed in its description is complete. Every feature must include proportional implementation, tests, documentation, examples, and a completion gate; only runtime features require a versioned release gate.

## Feature catalog

- [`phase-4` — Reliable mutations, Actor components, and defaults](completed/phase-4.md) — Add reliable mutation delivery, Actor component editing, and Blueprint/component defaults.
- [`phase-5` — Blueprint member variables](completed/phase-5.md) — Add typed Blueprint member-variable inspection and editing.
- [`phase-6` — Function signatures and local variables](completed/phase-6.md) — Add function signatures, function shells, and local variables.
- [`phase-7` — Macros and custom events](completed/phase-7.md) — Add macro and custom-event shells with matching inspection.
- [`phase-8` — Action-catalog infrastructure and core actions](completed/phase-8.md) — Add the bounded action-catalog infrastructure and core actions.
- [`phase-9` — C++ architecture and test decomposition](completed/phase-9.md) — Split oversized native components and Automation Tests along cohesive internal boundaries without changing behavior.
- [`phase-10` — Expanded action-catalog families](completed/phase-10.md) — Expand the action catalog to the remaining supported action families.
- [`phase-11` — Graph-node lifecycle](completed/phase-11.md) — Add transactional graph-node creation, movement, and removal.
- [`phase-12` — Pin defaults and direct connections](completed/phase-12.md) — Add pin defaults and direct graph connections without automatic conversion.
- [`phase-13` — Wildcards, conversions, and complete atomic graph editing](completed/phase-13.md) — Add wildcard specialization, explicit conversion insertion, and complete atomic graph editing.
- [`phase-14` — GameMode and GameState families](completed/phase-14.md) — Formalize GameMode and GameState family support.
- [`phase-15` — GameInstance family](completed/phase-15.md) — Add GameInstance family support.
- [`phase-16` — Multiplayer Blueprint authoring and framework assignment](completed/phase-16.md) — Add RPC custom events, replication settings, and narrow GameMode/GameInstance project assignment.
- [`phase-17` — User-defined structs and Data Tables](completed/phase-17.md) — Add bounded row-schema and typed game-design table authoring.
- [`asset-references` — Find asset references](completed/asset-references.md) — Find bounded serialized and live-memory referencers for one exact mounted asset.
- [`asset-delete` — Delete asset](completed/asset-delete.md) — Safely delete one exact unreferenced asset package through Unreal Editor.
  - Depends on:
    - `asset-references`
- [`widget-tree` — Widget Blueprint family and widget trees](completed/widget-tree.md) — Add Widget Blueprint creation, inspection, compilation, saving, and widget-tree editing.
- [`umg-authoring` — UMG layout, styling, bindings, and UI logic](completed/umg-authoring.md) — Complete practical HUD and menu authoring on the Widget Blueprint family.
  - Depends on:
    - `widget-tree`
- [`companion-plugins` — Companion plugin extension foundation](completed/companion-plugins.md) — Discover API-compatible independently versioned companion plugins and safely register bounded support for additional asset types, component types, and operations on existing assets.
  - Depends on:
    - `readonly-mode`
- [`asset-inspect-core` — General asset inspection foundation and gameplay Blueprints](completed/asset-inspect-core.md) — Add the common read-only facade and deep inspection for gameplay-framework and Interface Blueprints.
- [`native-wire-contracts` — Typed native request and result records](completed/native-wire-contracts.md) — Move native domain request/result contracts behind explicit JSON codecs.
- [`native-command-catalog` — Fixed native command routing and capabilities](completed/native-command-catalog.md) — Route fixed typed commands and capabilities without domain-specific bridge composition.
  - Depends on:
    - `native-wire-contracts`
- [`asset-family-foundation` — Built-in asset-family adapter contracts](completed/asset-family-foundation.md) — Add deterministic inspection, creation, and editing adapter contracts.
  - Depends on:
    - `asset-inspect-core`
    - `native-command-catalog`
- [`asset-inspection-adapters` — Asset inspection service decomposition](completed/asset-inspection-adapters.md) — Move existing core asset semantics into focused adapters.
  - Depends on:
    - `asset-family-foundation`
- [`asset-authoring-kernel` — Shared asset creation and editing lifecycle](completed/asset-authoring-kernel.md) — Centralize bounded admission, transactions, persistence, read-back, cleanup, and rollback.
  - Depends on:
    - `asset-family-foundation`
- [`python-asset-family-catalog` — Static Python asset-family catalog](completed/python-asset-family-catalog.md) — Compose approved schemas, access gates, mappings, and capabilities from one shipped catalog.
  - Depends on:
    - `asset-family-foundation`
- [`asset-family-conformance` — Reusable asset-family verification](completed/asset-family-conformance.md) — Parameterize common inspection, authoring, unavailable-state, persistence, and recovery checks.
  - Depends on:
    - `asset-inspection-adapters`
    - `asset-authoring-kernel`
    - `python-asset-family-catalog`
- [`native-domain-modules` — Native domain module boundaries](completed/native-domain-modules.md) — Isolate host, asset-core, Blueprint, UMG, and content ownership behind fixed registries.
  - Depends on:
    - `asset-family-conformance`
- [`companion-api-v2` — Typed asset-family companion API](completed/companion-api-v2.md) — Add typed inspection, creation, editing, read-back, persistence, and capability seams.
  - Depends on:
    - `companion-plugins`
    - `native-domain-modules`
    - `python-asset-family-catalog`
- [`companion-asset-adapters` — Unified companion asset integration](completed/companion-asset-adapters.md) — Route approved GAS and CommonUI families through common asset infrastructure.
  - Depends on:
    - `companion-api-v2`
    - `commonui-assets-inspect`
    - `gas-ability-blueprints-inspect`
    - `gas-gameplay-effects-inspect`
- [`asset-inspect-data` — Data Asset and Data Table inspection](completed/asset-inspect-data.md) — Extend the facade to structured Data Assets and Data Tables.
  - Depends on:
    - `asset-inspect-core`
    - `native-domain-modules`
- [`asset-inspect-umg` — Base UMG Widget Blueprint inspection](completed/asset-inspect-umg.md) — Extend the facade to base UMG logic, hierarchy, layout and bindings.
  - Depends on:
    - `asset-inspect-core`
    - `native-domain-modules`
- [`asset-inspect-animation` — Animation Blueprint semantic inspection](planned/asset-inspect-animation.md) — Extend the facade to pose graphs, layers and state machines.
  - Depends on:
    - `asset-inspect-core`
    - `native-domain-modules`
- [`ai-assets-inspect` — Unreal AI asset inspection](planned/ai-assets-inspect.md) — Inspect Behavior Trees, Blackboards, Environment Queries, and supported custom AI Blueprint node classes through `asset_inspect`.
  - Depends on:
    - `asset-inspect-core`
    - `companion-asset-adapters`
- [`enhanced-input-assets-inspect` — Enhanced Input asset inspection](planned/enhanced-input-assets-inspect.md) — Inspect Input Actions, Mapping Contexts, and supported trigger/modifier types through `asset_inspect`.
  - Depends on:
    - `asset-inspect-core`
    - `companion-asset-adapters`
- [`umg-mvvm-inspect` — UMG ViewModel and View Binding inspection](planned/umg-mvvm-inspect.md) — Inspect MVVM ViewModel Blueprints and Widget View Bindings through `asset_inspect` and an optional companion plugin.
  - Depends on:
    - `umg-authoring`
    - `asset-inspect-umg`
    - `companion-asset-adapters`
- [`umg-mvvm` — UMG ViewModel and View Binding authoring](planned/umg-mvvm.md) — Add typed MVVM ViewModel creation and Widget View Binding authoring through the MVVM companion.
  - Depends on:
    - `umg-mvvm-inspect`
    - `asset-authoring-kernel`
    - `companion-api-v2`
- [`commonui-assets-inspect` — Inspect assets with CommonUI plugin dependencies](completed/commonui-assets-inspect.md) — Add bounded typed inspection of supported CommonUI-dependent assets through an optional API-compatible companion plugin.
  - Depends on:
    - `umg-authoring`
    - `companion-plugins`
- [`commonui-umg-types-inspect` — CommonUI types in UMG inspection](completed/commonui-umg-types-inspect.md) — Expand `asset_inspect` from CommonUI root Widget Blueprints to supported CommonUI widget and value types throughout UMG trees.
  - Depends on:
    - `asset-inspect-umg`
    - `commonui-assets-inspect`
    - `companion-asset-adapters`
- [`commonui-assets-authoring` — Create/update assets with CommonUI plugin dependencies](planned/commonui-assets-authoring.md) — Add stale-safe creation and updating of supported CommonUI-dependent assets through the CommonUI companion.
  - Depends on:
    - `commonui-assets-inspect`
    - `asset-authoring-kernel`
    - `companion-asset-adapters`
- [`gameplay-tag-properties` — Gameplay Tag property values](completed/gameplay-tag-properties.md) — Read and set exact `FGameplayTag` and `FGameplayTagContainer` values through existing asset property workflows.
  - Depends on:
    - `native-wire-contracts`
    - `asset-authoring-kernel`
- [`gas-ability-blueprints-inspect` — Gameplay Ability Blueprint inspection](completed/gas-ability-blueprints-inspect.md) — Add bounded typed inspection of existing Gameplay Ability Blueprint assets through an optional API-compatible companion plugin.
  - Depends on:
    - `companion-plugins`
- [`windows-deployment-codex-preview` — Codex deployment configuration preview](completed/windows-deployment-codex-preview.md) — Show one shared launch definition as LM Studio JSON and separately copyable ChatGPT Codex STDIO fields.
  - Depends on:
    - `windows-deployment-install-modes`
- [`windows-deployment-commonui` — CommonUI companion deployment option](completed/windows-deployment-commonui.md) — Add an independent checkbox that builds and installs `UnrealMCPCommonUI` with the compatible base plugin.
  - Depends on:
    - `windows-deployment-install-modes`
    - `commonui-assets-inspect`
- [`windows-deployment-validation-fixes` — Deployment selection and Engine validation fixes](completed/windows-deployment-validation-fixes.md) — Support the complete three-plugin transaction and enforce Unreal Engine 5.8+ through one packaging validator.
  - Depends on:
    - `windows-deployment-commonui`
- [`python-tooling-decomposition` — Python support-tool responsibility decomposition](completed/python-tooling-decomposition.md) — Split Unreal-local packaging, deployment, and headless Python tools along narrow reusable responsibility boundaries without behavior changes.
  - Depends on:
    - `windows-deployment-validation-fixes`
- [`gas-ability-blueprints` — Gameplay Ability Blueprint creation and updating](planned/gas-ability-blueprints.md) — Add typed Gameplay Ability Blueprint creation and authoring through the GAS companion.
  - Depends on:
    - `phase-13`
    - `gas-ability-blueprints-inspect`
    - `asset-authoring-kernel`
    - `companion-asset-adapters`
- [`gas-gameplay-effects-inspect` — Gameplay Effect inspection](completed/gas-gameplay-effects-inspect.md) — Add bounded typed inspection of existing data-only Gameplay Effect Blueprint assets through the GAS companion.
  - Depends on:
    - `gas-ability-blueprints-inspect`
- [`gas-supporting-assets-inspect` — Supporting Gameplay Ability System asset inspection](planned/gas-supporting-assets-inspect.md) — Inspect Gameplay Cue Notify, Attribute Set, magnitude-calculation, and execution-calculation Blueprint assets through `asset_inspect`.
  - Depends on:
    - `gas-ability-blueprints-inspect`
    - `gas-gameplay-effects-inspect`
    - `companion-asset-adapters`
- [`gas-gameplay-effects` — Gameplay Effect creation and updating](planned/gas-gameplay-effects.md) — Add typed creation and data-only updating of Gameplay Effect Blueprint assets through the GAS companion.
  - Depends on:
    - `gas-gameplay-effects-inspect`
    - `asset-authoring-kernel`
    - `companion-asset-adapters`
- [`function-replace` — Complete function replacement](completed/function-replace.md) — Add transactional replacement of one complete user-owned function.
- [`event-macro-replace` — Event, custom-event, and macro replacement](completed/event-macro-replace.md) — Extend bounded replacement to events, custom events, and macros.
  - Depends on:
    - `function-replace`
- [`node-layout` — Deterministic changed-node layout](completed/node-layout.md) — Add deterministic layout for changed nodes.
  - Depends on:
    - `event-macro-replace`
- [`pcg-graph-inspect` — Procedural Content Generation graph inspection](planned/pcg-graph-inspect.md) — Inspect PCG Graph assets; review and update the detailed contract before implementation.
  - Depends on:
    - `companion-api-v2`
- [`pcg-graph-authoring` — Procedural Content Generation graph authoring](planned/pcg-graph-authoring.md) — Author PCG Graph assets; review and update the detailed contract before implementation.
  - Depends on:
    - `pcg-graph-inspect`
    - `asset-authoring-kernel`
    - `companion-api-v2`
- [`editor-launch` — Optional configured editor launch](completed/editor-launch.md) — Add optional configured editor launch.
- [`editor-shutdown` — Optional graceful editor shutdown](completed/editor-shutdown.md) — Add optional graceful editor shutdown.
  - Depends on:
    - `editor-launch`
- [`editor-restart` — Optional durable editor restart](completed/editor-restart.md) — Add optional durable editor restart.
  - Depends on:
    - `editor-launch`
    - `editor-shutdown`
- [`offline-capabilities` — Offline project identity in capabilities](completed/offline-capabilities.md) — Return configured project identity and explicit native availability before Unreal starts.
- [`readonly-mode` — Readonly mode and explicit writable access](completed/readonly-mode.md) — Make inspection-only operation the default, require `--writable` for project-content mutation, and simplify lifecycle opt-in.
  - Depends on:
    - `editor-restart`
- [`project-files` — Optional editor-offline project-file generation](planned/project-files.md) — Add optional editor-offline project-file generation.
  - Depends on:
    - `editor-restart`
- [`editor-build` — Optional editor-target builds](planned/editor-build.md) — Add optional editor-target builds.
  - Depends on:
    - `project-files`
- [`level-open` — Level discovery, safe opening, and snapshot foundations](completed/level-open.md) — Add bounded map discovery, explicit safe map opening, and restart-stable level snapshots.
  - Depends on:
    - [`issue-1` resolution](../issues/issue-1.md) — resolved in 0.17.1
- [`level-management` — Level management](completed/level-management.md) — Create, configure, save, and safely delete exact map assets.
  - Depends on:
    - `level-open`
    - `asset-delete`
- [`level-inspect` — World Partition actor and instance inspection](completed/level-inspect.md) — Inspect bounded descriptor, actor, component, and reflected instance state without loading the entire world.
  - Depends on:
    - `level-open`
- [`level-edit` — Transactional level actor editing and verified saving](completed/level-edit.md) — Add stale-safe actor batches and honest per-package World Partition save verification.
  - Depends on:
    - `level-inspect`
- [`pcg-component-edit` — Level PCG node inspection and authoring](planned/pcg-component-edit.md) — Inspect and author PCG-related level actors, components, and other supported level objects; review and update the detailed contract before implementation.
  - Depends on:
    - `pcg-graph-authoring`
    - `level-edit`
- [`spline-edit` — Spline component inspection and editing](planned/spline-edit.md) — Add bounded mixed-point spline inspection, mutation, persistence, and metadata safety.
  - Depends on:
    - `level-edit`
- [`pie-lifecycle` — Retained operations and single-process multiplayer PIE lifecycle](planned/pie-lifecycle.md) — Start and stop observable single-process PIE sessions, including a listen server and remote client.
  - Depends on:
    - `level-open`
- [`pie-inspect` — Per-world runtime actor inspection and attributed diagnostics](planned/pie-inspect.md) — Inspect exact server/client worlds with session-scoped actor identities and proven log attribution.
  - Depends on:
    - `pie-lifecycle`
- [`pie-test` — Bounded PIE test commands, waits, and multiplayer acceptance](planned/pie-test.md) — Add allowlisted test actions and complete a reusable single-process multiplayer acceptance flow.
  - Depends on:
    - `spline-edit`
    - `pie-inspect`
- [`pie-multiprocess` — Multi-process PIE companion and cross-process observation](planned/pie-multiprocess.md) — Extend retained sessions through an authenticated local runtime companion for owned PIE processes.
  - Depends on:
    - `pie-test`

## Shared roadmap contracts

### Process boundary

The default installation remains an exact-version pair:

1. A dependency-free Python 3.10+ MCP server using stdio JSON-RPC.
2. An Unreal Editor C++ plugin using public editor APIs and a bounded authenticated localhost HTTP bridge.

`companion-plugins` adds the base-owned extension registry and discovery contract used by every optional editor companion. Companions extend existing bounded tool families through exact allowlisted extension IDs; they do not add listeners, credentials, HTTP routes, arbitrary MCP tools, runtime-provided schemas, or unrestricted reflection. The Python package and base plugin remain authoritative for model-facing schemas, access classification, authentication, dispatch, limits, errors, capability composition, and extension admission.

`gas-ability-blueprints-inspect`, `gas-gameplay-effects-inspect`, and `gas-supporting-assets-inspect` use one optional editor-only `UnrealMCPGAS` companion plugin. It owns every direct Gameplay Ability System module dependency, reuses the base plugin's listener, credential, dispatch, ledger, and capability contracts, and has an independent semantic version while requiring the same `companion_api_version` as `UnrealMCP`. The base plugin must continue to build, package, load, and expose its complete non-GAS contract when the companion or Gameplay Ability System plugin is absent.

`umg-mvvm-inspect` adds an independent optional editor-only `UnrealMCPMVVM` companion plugin. It owns every direct `ModelViewViewModel` plugin and module dependency, reuses the same base extension and bridge contracts, and has an independent semantic version while requiring the same `companion_api_version` as `UnrealMCP`. The base plugin must retain its complete Widget Blueprint, legacy property-binding, and Designer-event contract when the companion or Engine UMG Viewmodel plugin is absent.

`commonui-assets-inspect` and `commonui-umg-types-inspect` use the independent optional editor-only `UnrealMCPCommonUI` companion plugin. It owns every direct `CommonUI` plugin and module dependency, reuses the same base extension and bridge contracts, and has an independent semantic version while requiring the same `companion_api_version` as `UnrealMCP`. CommonUI capabilities remain unavailable unless Unreal reports the `CommonUI` plugin effectively enabled for the configured project and its required modules loaded. The base plugin must retain its complete non-CommonUI contract when the companion is absent or CommonUI is missing, disabled, or unloaded.

`ai-assets-inspect` and `enhanced-input-assets-inspect` add independent optional editor-only `UnrealMCPAI` and `UnrealMCPEnhancedInput` companions. Each owns its direct Engine module or plugin dependencies, contributes only fixed `asset_inspect` families and selectors through the released companion adapter seam, and leaves the base plugin's complete contract available when absent or unready.

`pcg-graph-inspect` adds an independent optional editor-only `UnrealMCPPCG` companion plugin. It owns every direct PCG plugin and module dependency, reuses the same base extension and bridge contracts, and has an independent semantic version while requiring the same `companion_api_version` as `UnrealMCP`. PCG capabilities remain unavailable unless Unreal reports its `PCG` plugin effectively enabled for the configured project and the editor loads it successfully; an Engine-default enablement is valid, while an explicit project disablement wins. The base plugin must retain its complete non-PCG contract when the companion is absent or the Engine plugin is missing, disabled, or unloaded.

`pie-multiprocess` adds a minimal exact-version runtime companion module to the same plugin distribution for editor-owned multi-process PIE children. It connects outward to editor-owned authenticated IPC and does not expose a model-facing game listener.

Python owns MCP framing, published schemas, exact argument validation, readonly/writable access filtering, project configuration, discovery, authenticated HTTP calls, timeouts, process orchestration, and stable error presentation. The C++ plugin owns credentials, listener lifecycle, authentication, Game-thread dispatch, Unreal object access, Blueprint operations, compiler diagnostics, transactions, package saving, mutation-result retention, and authoritative native capabilities.

Deploy the Python package and base C++ plugin as an exact-version pair. Report both versions and the installed Unreal version through `capabilities`; reject a mismatched pair before mutation. Version every optional editor companion independently. Require its descriptor and compiled `companion_api_version` to match each other and the base descriptor and compiled value exactly, report its semantic version, API version, and readiness through `capabilities`, and reject all of its operations on any disagreement. Engine-plugin dependencies such as Gameplay Ability System, UMG Viewmodel, and PCG remain tied to the exact supported Unreal build rather than receiving an `UnrealMCP` semantic-version pin. Support for later Unreal releases must be proven through compilation and behavioral tests, not inferred from version numbers.

### Remaining model-facing tool surface

Keep the public surface compact. Add typed operations to these remaining tool families rather than publishing a separate tool for every native handler:

| Tool | First feature | Responsibility |
| --- | --- | --- |
| `operation_status` | `phase-4` | Resolve the retained outcome of one mutation operation without executing it again |
| `operation_cancel` | `readonly-mode` | Request safe cancellation of one retained mutation without mixing write authority into status lookup |
| `blueprint_component_edit` | `phase-4` | Perform one typed component-hierarchy or component-default mutation |
| `blueprint_default_edit` | `phase-4` | Set one supported Blueprint class-default property |
| `blueprint_member_edit` | `phase-5` | Perform one typed variable, function, macro, or custom-event mutation |
| `blueprint_action_catalog` | `phase-8` | Discover a bounded set of context-valid graph actions without mutation |
| `blueprint_graph_edit` | `phase-11` | Perform one typed node, pin, connection, position, or removal mutation |
| `gameplay_framework_edit` | `phase-16` | Assign only the configured project's default GameMode or GameInstance class |
| `game_data_inspect` | `phase-17` | Inspect one bounded user-defined struct or Data Table schema/row page |
| `game_data_edit` | `phase-17` | Create or mutate one bounded user-defined struct or Data Table transaction |
| `asset_references` | `asset-references` | Find bounded serialized and live-memory referencers for one exact mounted asset |
| `asset_delete` | `asset-delete` | Delete one exact unreferenced asset package through a retained, verified editor operation |
| `widget_tree_edit` | `widget-tree` | Perform one typed Widget Blueprint tree, widget-default, slot, layout, style, or binding mutation |
| `blueprint_block_replace` | `function-replace` | Replace one complete bounded logic unit as a prevalidated transaction |
| `editor_lifecycle` | `editor-launch` | Run one opt-in configured launch, restart, or graceful-shutdown operation |
| `project_build` | `project-files` | Run one opt-in configured project-generation or editor-target build operation |
| `level_inspect` | `level-inspect` | Discover mounted maps and inspect bounded current-map, actor, component, property, and spline snapshot pages |
| `level_open` | `level-open` | Safely open one exact mounted map without implicit save or discard |
| `level_manage` | `level-management` | Create and configure one exact map; map deletion reuses the safe `asset_delete` operation |
| `level_actor_edit` | `level-edit` | Apply one stale-safe bounded actor/component/spline mutation batch in the current map |
| `level_save` | `level-edit` | Save and verify the current map and explicit affected external-actor packages |
| `play_session_start` | `pie-lifecycle` | Start one retained bounded PIE topology with exact effective settings and instance identities |
| `play_session_stop` | `pie-lifecycle` | Idempotently stop one retained PIE session and report cleanup |
| `play_session_inspect` | `pie-inspect` | Inspect one retained session or exact runtime world with bounded actor/property/log pages |
| `play_session_command` | `pie-test` | Run one allowlisted test action or bounded wait against an exact retained session instance |

Lifecycle and future build tools remain absent unless independently configured. Readonly access is the default; complete Blueprint authoring requires the explicit `--writable` startup trust decision. Measure the Blueprint schemas and use nested operation discriminators if context cost becomes excessive.

The GAS features extend `asset_inspect` and existing Blueprint tools when the companion capability is live and do not add a separate model-facing GAS tool. `gas-ability-blueprints-inspect` and `gas-gameplay-effects-inspect` provide the released Ability and Effect families. `gas-supporting-assets-inspect` adds Cue Notify, Attribute Set, magnitude-calculation, and execution-calculation Blueprint families without treating Ability Tasks as standalone assets. The authoring features add only their explicitly named mutation surfaces. Capabilities distinguish read support from mutation support so an inspection-only release cannot advertise or execute create/update operations.

The MVVM features extend `asset_inspect` and existing Blueprint and Widget tools rather than adding a separate model-facing MVVM tool. `umg-mvvm-inspect` adds bounded typed inspection for ViewModel Blueprints, Widget Blueprint ViewModel contexts, and View Bindings while keeping those records distinct from legacy property bindings and Designer events. `umg-mvvm` then adds ViewModel Blueprint creation and editing plus typed `widget_tree_edit` mutations for contexts and bindings. Capabilities distinguish read support from mutation support so an inspection-only release cannot advertise or execute create/update operations.

The CommonUI features extend `asset_inspect` and existing Blueprint and Widget tools rather than adding a separate model-facing CommonUI tool. The released `commonui-assets-inspect` feature allowlists `UCommonUserWidget`-derived Widget Blueprints and adds bounded typed widget, activation, and reference records. `commonui-umg-types-inspect` expands semantic coverage to allowlisted CommonUI child widgets and stored CommonUI value types in any otherwise supported Widget Blueprint. `commonui-assets-authoring` can later add creation and updating only for inspected records that public editor APIs can mutate safely.

The AI and Enhanced Input inspection features add only fixed `asset_inspect` families and nested semantic records. AI scope is bounded to Behavior Trees, Blackboards, EQS, and named custom Blueprint node bases; Enhanced Input scope is bounded to actions, mapping contexts, legacy player-mappable configs, and named trigger/modifier types. Runtime execution, simulation, debugger state, project settings, and unrestricted reflection remain excluded.

Review and update every detailed PCG contract against the executable tool catalog, companion foundation, and supported Unreal public APIs before implementation. The only stable functional scope is inspection and authoring of PCG Graph assets plus inspection and authoring of PCG-related level actors, components, and other supported level objects. Current tool mappings, operation shapes, snapshots, validation, asynchronous lifecycle, persistence, limits, and verification details are provisional.

### Mutation delivery and concurrency contracts

- Require a caller-generated `operation_id` for every mutating call, including existing mutation tools. Bind it to the exact normalized arguments, project identity, bridge instance, and authenticated client context.
- Retain a bounded operation ledger with published capacity and lifetime limits. Repeating an operation ID with the same request returns the retained result; reusing it with different arguments returns a stable conflict and never executes.
- Publish explicit operation states such as `queued`, `executing`, `committed`, `rejected`, and `outcome_unknown`. Never report cancellation after a mutation has committed.
- Writable `operation_cancel` may remove queued work or stop preflight work, but it must not interrupt an active Unreal mutation at an unsafe point. A lost response must be reconciled through readonly `operation_status` before retry.
- The ledger is process-scoped unless a later operation explicitly defines durable restart state. If the bridge instance changes and no result is available, return `outcome_unknown` and require inspection before further mutation.
- Retained PIE lifecycle and wait operations may remain nonterminal across HTTP requests. Publish bounded starting/running/stopping progress, allow cancellation only at safe points, and keep terminal replay semantics identical to short mutations.
- Reject mutation while the target asset is compiling, saving, loading, being reinstanced, undergoing undo/redo, or otherwise unable to provide stable preconditions.
- Use one editor transaction per accepted atomic asset mutation where Unreal supports it. Prevalidate before opening it, verify postconditions before commit, and implement explicit restoration for unexpected failure. Config-file operations must use atomic persistence and verified restoration instead of pretending to be editor transactions. Do not assume that cancelling a transaction restores arbitrary object state.

### Blueprint identity, type, and property contracts

- Use Unreal long package names and object/class paths at the model boundary; reject raw filesystem paths and traversal.
- Read-only operations may inspect any content mount visible to the project. An asset being mutated must remain confined to `/Game` or a content mount owned by a plugin physically inside the current project's local `Plugins/` directory.
- A referenced class or asset is not itself a mutation target. Permit type-compatible native classes and packageable assets from visible mounted content while rejecting transient, editor-only, unresolved, incompatible, or unsafe references.
- Give components, Blueprint members, struct members, table schemas/rows, widgets/slots, graphs, nodes, pins, inspection snapshots, and bridge instances explicit identities. Mutation targets must have stable identities; an unavailable identity is not silently replaced by a name lookup.
- Require the current structural snapshot and all relevant object identities for mutation. Return `stale_precondition` instead of retargeting reconstructed or replacement objects.
- Reuse one bounded canonical K2 type and reflected-property codec across inspection and mutation. Add a type or value form only with read/write round-trip tests and explicit unsupported behavior.
- Validate every MCP argument against its published schema in Python and again against the live Unreal object, graph schema, property metadata, and family capabilities in C++.
- Use one stable bounded error envelope with `code`, `message`, `details`, and `retryable`. Never return C++ exceptions, assertions, addresses, credentials, or unbounded logs.
- Keep request bodies, JSON depth, strings, collections, scans, caches, operation state, diagnostics, response bytes, transaction work, and Game-thread time explicitly bounded and published through `capabilities`.

### Level authoring contracts

- Keep `level_inspect` read-only and use the separate ledger-backed `level_open` operation for map switching. Never implicitly save, discard, or prompt for dirty work.
- Address maps by mounted `UWorld` asset paths, never raw `.umap` filesystem paths. Creation and deletion must verify the complete map-owned package set, including World Partition external packages where applicable.
- Qualify stable Actor GUID and component identities by exact map identity. Require the current map snapshot and exact identities for every existing-object mutation.
- Use World Partition actor descriptors for bounded discovery and exact or region loading for live instance work. Missing or failed cells and data layers are errors, not evidence that an actor is absent.
- Prevalidate each complete actor/spline batch, transact where Unreal supports it, maintain explicit restoration for unexpected in-memory failure, and verify postconditions before reporting commit.
- Treat a multi-package save as a verified batch, not an atomic filesystem transaction. Return per-package outcomes and explicit partial failure; never claim that Unreal rolled back already persisted external packages.

### Retained PIE session contracts

- Give sessions, operations, instances, world contexts, runtime actors, logs, and tests explicit bounded identities. Require both session and instance/world identity for every world-specific query or action.
- A two-player listen server has a listen-server/host world and one remote-client world. Use a dedicated server plus two clients when three separate worlds are required.
- Publish supported topology, player/client, process-mode, inspection, command, wait, and test policies through `capabilities`; reject unsupported modes rather than falling back to mutable editor preferences.
- Single-process inspection remains editor-owned. Multi-process observation requires the `pie-multiprocess` exact-version companion and authenticated editor-owned IPC; never guess or scrape state from foreign child processes.
- Allow only configured console commands, named tests, plugin-marked reflected test functions, and bounded predicates. Do not expose arbitrary input injection, `ProcessEvent`, runtime reflection mutation, or unrestricted console execution.
- Return only diagnostics with a proven originating process/PIE instance. Exclude unattributable raw log entries instead of assigning them heuristically.

### Security baseline

- Bind only to `127.0.0.1` and verify the actual listening address in native integration tests.
- Authenticate every request with the high-entropy per-project credential and fail closed on credential, listener, route, or heartbeat faults.
- Never expose the credential or absolute project path through discovery, capabilities, operation records, diagnostics, or logs.
- Permit one bridge owner per configured port; bound queued requests and retained state; and cleanly release route, discovery, credentials, and pending work during shutdown.
- Never expose arbitrary UObject calls, unrestricted reflection mutation, Python execution inside Unreal, unrestricted console commands, supplied C++, arbitrary subprocess arguments, or general filesystem/process access. Retained PIE commands remain confined to the capability-advertised test allowlists and exact plugin-marked functions.

### Release discipline

For runtime work, increment the minor version after each completed feature and the patch version for fixes or behavior-preserving refactoring. A major-version promotion requires a separate explicit decision. Keep Python metadata, `.uplugin` metadata, runtime capabilities, tests, README, examples, and `HISTORY.md` synchronized after every runtime feature. Feature documents do not repeat version-update tasks.

## Deferred and excluded scope

The following are not part of the committed remaining roadmap unless separately authorized:

- Arbitrary selected-region block replacement beyond the complete logic-unit boundaries supported in `function-replace` and `event-macro-replace`.
- General filesystem access or C++ source modification.
- Arbitrary shell commands, compiler arguments, console commands, UObject calls, unrestricted reflection mutation, expressions, or supplied-code evaluation.
- Unrestricted whole-Blueprint text import/export or wholesale Blueprint replacement.
- Blueprint reparenting, project-settings mutation beyond the narrow `phase-16` gameplay-framework assignments, timelines, event-dispatcher authoring, interface authoring, and specialized asset families not named in this roadmap.
- Level Blueprint, Animation Blueprint, Control Rig, Niagara, Material, Behavior Tree, StateTree, or Widget-animation authoring.
- General Play-in-Editor input injection, screenshots, runtime object mutation beyond exact configured test functions, arbitrary gameplay assertions, or unrestricted raw-log capture.
- Cloud services, accounts, telemetry, dependency downloads, or a model-facing game-side network listener.

## Primary Unreal 5.8 API references

These references establish feasibility only. Each owning feature must add compiled public-header probes and behavioral tests before freezing its model-facing contract:

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
