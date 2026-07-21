# Unreal Editor MCP Roadmap

This roadmap turns the requirements in `docs/draft.md` into sequential, releasable implementation phases. The target is Unreal Engine 5.8 and newer. macOS is the first native development and validation host; native Windows qualification is mandatory before the first stable release.

## Top-level checklist

- [x] Phase 1 — Establish the versioned Python/C++ foundation and authenticated bridge.
- [x] Phase 2 — Add bounded, read-only Actor Blueprint discovery and inspection.
- [ ] Phase 3 — Create, compile, save, and verify new Actor Blueprints safely.
- [ ] Phase 4 — Edit Actor Blueprint component hierarchies and component defaults.
- [ ] Phase 5 — Edit Blueprint variables, functions, macros, and custom-event shells.
- [ ] Phase 6 — Discover valid Blueprint actions and create graph nodes atomically.
- [ ] Phase 7 — Complete atomic graph wiring, pin defaults, movement, and removal.
- [ ] Phase 8 — Harden the complete Actor Blueprint authoring workflow for beta use.
- [ ] Phase 9 — Extend the workflow to GameMode, GameState, and GameInstance families.
- [ ] Phase 10 — Add transactional replacement of one bounded Blueprint logic block.
- [ ] Phase 11 — Add low-priority editor launch, restart, and shutdown workflows.
- [ ] Phase 12 — Add low-priority, editor-offline C++ project build orchestration.
- [ ] Phase 13 — Qualify Windows natively and publish the first stable release.

Keep this checklist synchronized one-for-one with the phase sections below. Complete phases in order unless a phase explicitly permits parallel validation work. Every completed phase must include implementation, tests, documentation, examples, and a releasable version update.

## Architectural baseline

### Process boundary

The application has two exact-version components:

1. A dependency-free Python 3.10+ MCP server using stdio JSON-RPC.
2. An Unreal Editor C++ plugin using Unreal's editor APIs and a bounded localhost HTTP bridge.

The Python package owns MCP framing, published schemas, exact argument validation, tool modes, project configuration, discovery, authenticated HTTP calls, timeouts, and stable error presentation. The C++ plugin owns credential persistence, listener lifecycle, request authentication, Unreal Game-thread dispatch, Blueprint operations, compiler diagnostics, transactions, saving, and authoritative capabilities.

Deploy the Python package and C++ plugin as an exact-version pair. Report both versions and the installed Unreal version through `capabilities`; reject a mismatched pair before mutation. Support for later Unreal releases must be proven through compilation and behavioral tests, not assumed from version numbers.

### Proposed source layout

The first phase should establish this responsibility-oriented layout and document the verified dependency map through the indexed component and type references under `docs/`:

```text
unreal-editor-mcp/
  unreal_editor_mcp/          Python package
  plugin/UnrealMCP/           Unreal editor plugin and module
  tests/                      Python unit and MCP integration tests
  plugin/Source/.../Tests/    Unreal automation and headless integration tests
  examples/                   LM Studio and project configuration examples
  docs/                       Indexed architecture and type references
  README.md
  HISTORY.md
  ROADMAP.md
```

Do not introduce a shared library with `godot-editor-mcp` until both applications need the same executable code. Reuse architectural lessons and test patterns, not source coupling.

### Initial model-facing tool surface

Keep the public surface compact. Add operations to these families over time instead of publishing a separate MCP tool for every C++ handler:

| Tool | First phase | Responsibility |
| --- | ---: | --- |
| `capabilities` | 1 | Versions, Unreal compatibility, active mode, commands, features, and effective limits |
| `editor_state` | 1 | Project identity, bridge readiness, editor state, and concise operation state |
| `blueprint_inspect` | 2 | Targeted, paginated Blueprint structure and graph inspection |
| `blueprint_create` | 3 | No-overwrite creation from a validated Actor-derived base class |
| `blueprint_compile` | 3 | Explicit compilation with bounded structured diagnostics |
| `blueprint_save` | 3 | Explicit package save with distinct failure reporting |
| `blueprint_component_edit` | 4 | One typed component-hierarchy or component-default mutation |
| `blueprint_member_edit` | 5 | One typed variable, function, macro, or custom-event mutation |
| `blueprint_action_catalog` | 6 | Targeted discovery of valid graph actions in the current context |
| `blueprint_graph_edit` | 6 | One typed node, pin, connection, position, or removal mutation |
| `blueprint_block_replace` | 10 | One bounded, prevalidated logic-block transaction |

Lifecycle and build tools remain absent from the model context until their late phases. Use nested tool modes if measurements show that the Blueprint action schemas impose excessive context cost; the default mode must still support the complete first Actor workflow by Phase 8.

### Cross-boundary contracts

- Use Unreal long package names and object/class paths at the model boundary; reject raw filesystem paths and traversal. Read-only operations may inspect any content mount visible to the project. Mutations must be confined to `/Game` and content mounts whose plugin base directory is physically inside the current project's local `Plugins/` directory; reject `/Engine`, engine/marketplace plugins outside the project, and other external mounts.
- Give graphs, nodes, pins, components, and inspection snapshots explicit identities. A mutation based on stale structure must return `stale_precondition` rather than retargeting a replacement object.
- Validate every MCP argument against its published schema in Python and again against the live Unreal object and schema in C++.
- Use one stable bounded error envelope with `code`, `message`, `details`, and `retryable`. Never return C++ exceptions, assertions, addresses, tokens, or unbounded logs.
- Keep request bodies, JSON nesting, strings, collection sizes, page sizes, outstanding requests, operation time, diagnostics, and response bytes explicitly bounded. Publish effective limits through `capabilities`.
- Accept HTTP work off-thread only long enough to authenticate, bound, and parse it. Dispatch every Unreal object access and mutation to the Game thread, then complete the retained HTTP response within a deadline.
- Never expose arbitrary UObject method calls, arbitrary reflection mutation, Python execution inside Unreal, console commands, supplied C++, or a general filesystem/process tool.

### Security baseline

- Bind only to `127.0.0.1`; verify the actual listening address in an integration test rather than trusting configuration intent.
- Generate a high-entropy token per Unreal project under ignored generated project state, persist it durably, and authenticate every HTTP request with a constant-time comparison.
- Fail closed: no usable listener, discovery heartbeat, or ready state may exist if token creation, reading, persistence, or re-reading fails.
- Publish only project-hash, process, port, version, and freshness data in the discovery record. Never publish the token or absolute project path.
- Permit one bridge owner per configured port, bound client and queued-operation counts, authentication and body deadlines, and clean route/listener shutdown during module unload.

### Release discipline

Start at `0.1.0` when Phase 1 is complete. Increment the minor version for each feature phase and the patch version for fixes. Phase 13 promotes the qualified release to `1.0.0`. Keep Python metadata, `.uplugin` metadata, runtime capabilities, tests, README, examples, and `HISTORY.md` synchronized after every phase.

## Phase 1 — Secure foundation and verified Unreal API boundary

**Outcome:** A releasable MCP/plugin pair can authenticate to one open Unreal 5.8 project and return `capabilities` and `editor_state`; it cannot mutate project content.

### Implementation

- Create the Python package, CLI, stdio MCP transport, dependency-free schema validator, tool catalog, dispatcher, bridge client, discovery reader, structured errors, and bounded shutdown behavior.
- Create the editor-only C++ plugin/module, version metadata, composition root, token store, authenticated startup gate, HTTP route owner, command router, error envelopes, limits, discovery heartbeat, and Game-thread request queue.
- Before freezing bridge interfaces, compile small probes against the installed Unreal 5.8 public headers for `HttpServer`, `IHttpRouter`, `FScopedTransaction`, `FKismetEditorUtilities`, `FBlueprintEditorUtils`, `USubobjectDataSubsystem`, `UEdGraphSchema_K2`, `UBlueprintNodeSpawner`, `FCompilerResultsLog`, `FAssetRegistryModule`, and package saving.
- Do not include headers from Unreal `Private` directories or patch engine source. Isolate unavoidable Unreal-version differences behind one compatibility adapter with a test for every compiled branch.
- Confirm empirically that Unreal's HTTP server can be constrained to loopback on macOS. If it cannot, stop the phase and replace only the listener adapter with a minimal loopback-only implementation; do not weaken the security requirement.
- Make `capabilities` authoritative for versions, exact commands, optional features, request/response limits, platform, Unreal version, and bridge readiness.

### Verification

- Use Python's built-in test framework for MCP initialization, `tools/list`, `tools/call`, schema rejection, stdout purity, error mapping, discovery validation, token handling, HTTP timeouts, and cancellation.
- Use Unreal Automation Tests for token persistence, duplicate route ownership, request bounds, authentication failure, error-envelope bounds, and Game-thread dispatch.
- Run a headless or command-line editor integration test that loads the plugin, connects through Python, calls both tools, rejects a bad token, confirms loopback-only listening, and unloads cleanly.
- Exercise macOS paths natively and Windows/Linux path and process branches through injected platform adapters in unit tests.

### Documentation and release gate

- Add `README.md`, `HISTORY.md`, source-backed architecture/type references under `docs/`, installation instructions, an LM Studio example, generated-state ignore guidance, and offline build/test instructions.
- Release `0.1.0` only when the Python/plugin versions match, the macOS integration test passes with Unreal 5.8, and no mutation command is registered.

## Phase 2 — Bounded Actor Blueprint discovery and inspection

**Outcome:** Agents can locate an Actor Blueprint and read only the relevant parts of its structure without changing or dirtying it.

### Implementation

- Add `blueprint_inspect` with explicit sections for summary, parent class, compile state, component hierarchy, variables, graphs, nodes, pins, and connections.
- Use the Asset Registry for bounded discovery across all content mounts visible to the project and exact object-path resolution; load only a specifically requested Blueprint for deep graph inspection.
- Add exact filters, shallow defaults, page limits, scan ceilings, opaque cursors, short cursor lifetime, and snapshot IDs. Bind every cursor to the normalized query and structural snapshot.
- Define compact type and value encodings for common K2 pins and reflected component defaults. Mark unsupported types explicitly instead of serializing arbitrary UObject graphs.
- Return stable graph, node, pin, and component identities where Unreal provides them; document and test identity behavior across compile, save, reload, undo, and node reconstruction.

### Verification

- Cover missing assets, non-Blueprint assets, non-Actor parents, inherited content, empty graphs, large synthetic graphs, unsupported values, cursor expiry, query mismatch, and structural staleness.
- Prove that inspection does not dirty the package, create a transaction, compile the Blueprint, or change editor selection.
- Compare structured results before and after save/reload using behavioral data from the test project, not prose fixtures.

### Documentation and release gate

- Document targeted queries, identity rules, pagination, result bounds, and context-efficient inspection examples.
- Release `0.2.0` when a known Actor Blueprint can be inspected headlessly and through the live editor with identical bounded contracts.

## Phase 3 — Safe Actor Blueprint creation, compile, save, and read-back

**Outcome:** Agents can create a new Actor Blueprint from a specific valid base class, compile it, save it, and verify it through inspection.

### Implementation

- Add `blueprint_create` using a `/Script/Module.Class` or validated Blueprint-generated parent reference and a destination confined to `/Game` or a content mount physically owned by a plugin under the current project's local `Plugins/` directory.
- Require an Actor-derived, Blueprint-compatible parent. Reject missing, abstract, deprecated, skeleton, reinstanced, editor-only, or otherwise unsuitable classes before package creation.
- Enforce atomic no-overwrite behavior. Validate the destination and parent first, then create in a controlled package, register only the intended asset, compile, and save without interactive dialogs. Define deterministic cleanup for any failure before publication.
- Add explicit `blueprint_compile` using a bounded compiler results log and `blueprint_save` using non-interactive package saving. Keep compile failure, save failure, source-control/read-only conflict, and transport failure distinct.
- Return the saved asset path, parent identity, compile state, package dirty state, snapshot ID, and bounded diagnostics. Require a separate inspection call for detailed read-back.

### Verification

- Test native and Blueprint-generated Actor parents, invalid classes, invalid mount paths, case/path normalization, existing destinations, race-like duplicate creation, read-only destinations, local project-plugin destinations, engine/external-plugin mutation rejection, compile failures, save failures, and cleanup.
- Restart the editor and confirm the asset loads, retains its parent, compiles, and produces the expected inspection snapshot.
- Confirm that an existing asset is never overwritten or deleted during failed creation.

### Documentation and release gate

- Add base-class and package-path examples, supported/unsupported parent rules, and compile/save troubleshooting.
- Release `0.3.0` when create → compile → save → restart → inspect succeeds without UI prompts on macOS.

## Phase 4 — Actor component hierarchy and defaults

**Outcome:** Agents can build the component structure required by an Actor before authoring its graph logic.

### Implementation

- Add `blueprint_component_edit` operations for one add, remove, rename, reparent, or default-property change.
- Prefer the public Subobject Data Subsystem and Simple Construction Script APIs proven in Phase 1. Keep inherited, native, instanced, and locally owned components distinguishable.
- Permit only `UActorComponent`-derived classes valid for the target Blueprint. Enforce unique names, legal attachment rules, root-component invariants, ownership, and stale snapshot preconditions.
- Add a bounded property codec for safe editable defaults, beginning with booleans, finite numbers, strings/names, enums/flags, common math/color structs, class references, and confined asset references. Reject arbitrary object graphs, delegates, raw pointers, and unsupported containers.
- Wrap every accepted mutation in one editor transaction, call the required Blueprint modification notifications, and return the new snapshot and concise changed-component record. Compilation and saving remain explicit tools.

### Verification

- Test scene and non-scene components, root creation/replacement rules, attachment cycles, duplicate names, inherited component rejection, incompatible properties, reference confinement, stale snapshots, undo, redo, compile, save, and reload.
- On any rejected request, verify that package dirty state, component hierarchy, transaction history, and compile state remain unchanged.

### Documentation and release gate

- Document component identities, ownership restrictions, supported value forms, explicit compile/save workflow, and recovery through Undo.
- Release `0.4.0` after creating and persisting a multi-component Actor Blueprint entirely through MCP calls.

## Phase 5 — Blueprint members and graph shells

**Outcome:** Agents can define the data and named logic containers needed by Actor Blueprint code.

### Implementation

- Add `blueprint_member_edit` operations for member variables, local variables, functions, macros, and custom-event shells.
- Start with a bounded K2 type vocabulary and explicit container/reference forms. Validate variable names, uniqueness, type compatibility, defaults, replication flags, instance editability, visibility, categories, and local scope.
- Create and remove function or macro graphs through public Blueprint utilities; preserve required entry/result nodes and validate signatures before mutation.
- Create custom events only in compatible graphs. Keep override events and inherited functions distinct from user-defined members and reject unsafe deletion or signature changes.
- Use one transaction per accepted mutation and return concise member/graph identities plus the new Blueprint snapshot.

### Verification

- Test every supported type and metadata flag, invalid names, collisions, inherited members, local-variable scope, signature changes, reference repair, remove-with-references behavior, undo/redo, compilation, saving, and reload.
- Reject mutations that would silently orphan existing nodes unless an explicit bounded reference policy is supplied and prevalidated.

### Documentation and release gate

- Document the type vocabulary, metadata, function/macro/event restrictions, and examples that inspect before editing.
- Release `0.5.0` when variables and empty logic containers survive compile, save, restart, and read-back.

## Phase 6 — Context-valid action discovery and node creation

**Outcome:** Agents can discover a small relevant set of valid actions for one graph and create supported Blueprint nodes without guessing C++ node classes.

### Implementation

- Add `blueprint_action_catalog` with exact text, owner class, function/member, node family, graph, and pin-context filters plus strict result and time limits.
- Return opaque action IDs bound to the target Blueprint class, graph schema, structural snapshot, and action signature. Do not expose an unrestricted node-class constructor.
- Build the catalog from Unreal's Blueprint action/spawner APIs and schema context. Initially support event, function-call, variable get/set, flow-control, cast, literal, and common operator families that pass the live graph filter.
- Add the `add_node` operation to `blueprint_graph_edit`. Invoke only a catalog action valid for the current snapshot and requested graph; assign a persistent node identity and bounded position.
- Mark the Blueprint structurally modified inside one transaction and return created node/pin identities. Keep connections and non-autogenerated pin defaults for Phase 7.

### Verification

- Test pure and impure functions, static and instance context, inherited members, event uniqueness, latent calls, incompatible graph types, expired action IDs, stale snapshots, catalog truncation, and action-spawn failure.
- Confirm that unsupported/internal actions cannot be invoked by forging names, class paths, or opaque IDs.

### Documentation and release gate

- Document the inspect → catalog → add-node workflow and teach agents to request narrow catalog filters rather than dumping the full action database.
- Release `0.6.0` when a representative Actor event graph can receive context-valid event, function, variable, and flow-control nodes and compile after explicit wiring is added manually in the editor.

## Phase 7 — Complete atomic graph editing

**Outcome:** Agents can implement Actor Blueprint logic through small, individually validated graph actions.

### Implementation

- Extend `blueprint_graph_edit` with `set_pin_default`, `connect_pins`, `disconnect_pins`, `remove_node`, and `move_node` operations. Add rename/comment operations only where they have stable public semantics.
- Use the live K2 graph schema for pin compatibility, connection responses, automatic conversion policy, default parsing, link breaking, and safe deletion. Never write pin link arrays or unvalidated default strings directly.
- Require Blueprint snapshot, graph identity, node identity, and pin identity preconditions. Return a conflict if reconstruction, compile, undo, or another edit replaced the target.
- Treat one MCP call as one editor transaction and one atomic mutation. Validate the complete requested action before opening the transaction; cancel or undo on unexpected postconditions.
- Bound graph size, links per pin, node position, default size, encoded result, retained diagnostics, transaction work, and Game-thread duration.

### Verification

- Test execution and data pins, compatible and incompatible types, optional automatic conversion, wildcard specialization, defaults, object/class/asset references, duplicate links, link replacement, node deletion, required entry/result nodes, stale identities, undo/redo, compile, save, and reload.
- Capture the graph before rejection and prove structural, dirty-state, and transaction-history equality afterward.

### Documentation and release gate

- Add atomic editing recipes and stable guidance for inspecting after node reconstruction or compilation.
- Release `0.7.0` when MCP calls alone can implement and persist a small BeginPlay-driven Actor behavior from existing components and variables.

## Phase 8 — Actor workflow hardening and beta release

**Outcome:** The full create/open → inspect → component/member/graph edit → compile → save → inspect workflow is dependable for supervised game-logic development.

### Implementation

- Normalize compiler warnings, errors, notes, node references, graph references, and source locations into bounded structured diagnostics.
- Add operation IDs, deadlines, cancellation, concise progress/state, diagnostic cursors, and deterministic behavior when the editor is busy compiling, saving, loading, undoing, or shutting down.
- Audit every mutation for prevalidation, transaction scope, postconditions, package dirty state, and unexpected partial changes. Add targeted rollback where Unreal's transaction system alone does not restore the required invariant.
- Measure tool-schema and representative result token costs. Tighten descriptions, defaults, filters, pages, and modes for small local models without hiding required safety fields.
- Add the canonical end-to-end sample project and behavioral integration suite. Tests must derive expected results from runtime contracts and created assets, not prose documentation.

### Verification

- Run repeated end-to-end sessions against clean and already-edited Actor Blueprints, including interruption, timeout, malformed requests, stale snapshots, compiler failures, save conflicts, editor restart, and undo/redo.
- Add fuzz/property-oriented Python schema tests and bounded C++ JSON/value decoder tests without third-party runtime dependencies.
- Record peak request/response size, retained state, startup time, and typical operation latency on the 16 GB macOS development machine.

### Documentation and release gate

- Complete the Actor-focused README, troubleshooting, security model, limits, mode guidance, LM Studio setup, and end-to-end tutorial.
- Release `0.8.0` as the Actor Blueprint beta only when the documented acceptance workflow passes from a clean project and against an existing Actor Blueprint.

## Phase 9 — Gameplay-framework Blueprint families

**Outcome:** The established safe workflow supports GameMode, GameState, and GameInstance-derived Blueprints without weakening Actor restrictions.

### Implementation

- Extend parent-class policy and capabilities to `AGameModeBase`/`AGameMode`, `AGameStateBase`/`AGameState`, and `UGameInstance` families.
- Reuse inspection, members, action catalog, graph editing, compile, save, and diagnostics. Enable component operations only for families and component ownership models proven valid; do not pretend `UGameInstance` is an Actor.
- Add family-specific event/function coverage and reject operations unavailable in a target graph or parent class through live schema checks.
- Keep output family-aware so agents can distinguish Actor component workflows from UObject-based framework Blueprints.

### Verification

- Create, edit, compile, save, restart, and inspect at least one Blueprint from every supported family and representative base class.
- Test inherited framework callbacks, class defaults, unsupported component operations, parent changes outside scope, and use of saved classes in project settings where performed manually.

### Documentation and release gate

- Document family capabilities and differences with focused examples; do not add project-settings mutation in this phase.
- Release `0.9.0` when all supported families pass the shared behavioral contract and their family-specific restrictions.

## Phase 10 — Bounded logic-block replacement and layout

**Outcome:** Agents can change one complete event-handler implementation, function, macro, or selected graph region as one prevalidated transaction while preserving unrelated content.

### Implementation

- Add `blueprint_block_replace` using the same member, action-catalog, and graph-edit primitives rather than a second unrestricted mutation engine.
- Define explicit block boundaries, owned nodes, required entry/exit identities, external links, local variables, limits, preconditions, and replacement operations. Do not accept free-form Blueprint text or supplied code.
- Preflight the complete operation on a transient duplicate or equivalent isolated representation, compile that candidate when Unreal permits, and compare postconditions before touching the live Blueprint.
- Apply the validated live change as one editor transaction. Preserve unrelated graphs, nodes, variables, metadata, links, positions, bookmarks, comments, and package state; cancel or undo on any unexpected result.
- Add a deterministic bounded layout algorithm for changed nodes only. Preserve untouched node positions and handle cycles, execution flow, data dependencies, comments, and graph bounds predictably.

### Verification

- Replace event handlers, functions, macros, and selected regions with internal and external data/control links; test invalid candidates, cycles, latent nodes, local variables, stale snapshots, compile failure, rollback, undo/redo, save/reload, and unchanged-content fingerprints.
- Prove that a failed preflight creates no transaction and that unexpected live failure restores the exact inspected structure and dirty state.

### Documentation and release gate

- Document block ownership, boundary links, layout behavior, preservation guarantees, limits, and when atomic actions remain preferable.
- Release `0.10.0` only when unrelated-content fingerprints remain stable across successful and rejected replacements.

## Phase 11 — Editor lifecycle workflows

**Outcome:** Agents can optionally launch, restart, and gracefully shut down only the configured Unreal project/editor instance.

### Implementation

- Add lifecycle tools only in an opt-in large mode. Accept no executable path, project path, or arbitrary command arguments from the model.
- Configure absolute Unreal editor and `.uproject` paths at MCP startup; validate both and expose only configured/available booleans through capabilities.
- Launch one detached editor instance using platform adapters, detect the project-specific authenticated bridge, and distinguish `starting`, `ready`, `already_running`, and failed startup.
- Implement graceful bridge-owned shutdown with dirty-package safeguards. Do not provide forced process termination.
- Implement restart as a persisted operation with exact project, plugin version, and operation identity; wait through disconnect, rediscovery, reauthentication, and ready-state confirmation.

### Verification

- Test missing/malformed paths, another project on the port, repeated launches, dirty packages, active compilation, graceful shutdown, restart success, stale restart records, version changes, timeouts, and cancellation.
- Validate macOS natively and keep Windows process behavior behind an injected, unit-tested adapter pending Phase 13.

### Documentation and release gate

- Document opt-in configuration, dirty-content policy, restart guarantees, and recovery steps.
- Release `0.11.0` when launch/restart/shutdown complete without arbitrary process execution or data loss on macOS.

## Phase 12 — Editor-offline C++ project orchestration

**Outcome:** Agents can run narrowly configured Unreal project-generation and editor-target build workflows only while the project editor is stopped.

### Implementation

- Keep C++ source editing outside this application. Pair with `rooted-files-mcp` for confined text edits.
- Add opt-in tools for project-file generation and an explicitly configured editor-target build. Resolve Unreal Build Tool or platform scripts from validated startup configuration and the installed engine layout.
- Use fixed command templates owned by platform adapters. The model may select only bounded targets/configurations from a published allowlist and may not supply executable paths, shell fragments, environment variables, or arbitrary arguments.
- Refuse build operations while the authenticated project editor is running. Bound process count, duration, captured stdout/stderr bytes, diagnostic records, and cancellation/cleanup behavior.
- Normalize compiler diagnostics and keep subprocess output off MCP stdout except inside valid tool results.

### Verification

- Test command construction for macOS and Windows, paths with spaces, missing tools, invalid targets/configurations, editor-running rejection, timeout, cancellation, nonzero exit, oversized logs, and process cleanup.
- Run the native macOS project-generation and editor-target build workflow against the test project without network access or runtime downloads.

### Documentation and release gate

- Document offline engine/tool preparation, configuration allowlists, interaction with editor lifecycle, and use with the confined file MCP.
- Release `0.12.0` when the fixed macOS workflows are reproducible and Windows command construction is fully unit tested.

## Phase 13 — Native Windows qualification and stable release

**Outcome:** The complete supported feature set is natively validated on macOS and Windows and released as `1.0.0`.

### Implementation

- Build and package the exact Python/plugin pair with Unreal 5.8 on a native Windows host. Add only source-evidenced compatibility fixes behind narrow platform adapters or Unreal-version checks.
- Validate token persistence and permissions, loopback binding, discovery, path casing and separators, JSON encoding, package paths, plugin loading, Game-thread dispatch, Blueprint transactions, compilation, saving, lifecycle processes, project-file generation, and editor-target builds.
- Run the complete Python, Unreal Automation, headless bridge, Blueprint behavioral, restart, and build suites natively on both macOS and Windows.
- Record platform and Unreal patch versions, package/install instructions, known limitations, performance/context measurements, and native results in README and history.

### Verification

- Require identical model-facing schemas, stable errors, limits, versions, and core Blueprint results across both platforms. Platform-specific filesystem/process details must not leak into normal tool contracts.
- Re-run the security audit: bad-token isolation, fail-closed credential faults, loopback-only listening, discovery secrecy, request bounds, timeouts, duplicate ownership, and shutdown cleanup.
- Perform a clean-machine offline installation test from packaged artifacts on both platforms.

### Documentation and release gate

- Publish `1.0.0` only when every earlier phase is complete, both native platform suites pass, all metadata and examples match, and no mandatory Windows result remains pending.
- Keep Linux source portability and unit-tested branches documented. Native Linux validation can become a later roadmap phase when a host is available.

## Deferred and excluded scope

The following are not part of the committed roadmap unless separately authorized:

- General filesystem access or C++ source modification.
- Arbitrary shell commands, compiler arguments, console commands, UObject calls, reflection mutation, expressions, or supplied-code evaluation.
- Unrestricted whole-Blueprint text import/export or wholesale Blueprint replacement.
- Level Blueprint, Widget Blueprint, Animation Blueprint, Control Rig, Niagara, Material, Behavior Tree, or StateTree authoring.
- Play-in-Editor input injection, screenshots, runtime object mutation, or automated gameplay assertions.
- Cloud services, accounts, telemetry, dependency downloads, or a game-side network listener.

## Primary Unreal 5.8 API references

These references establish feasibility, but the Phase 1 compiled probes and behavioral tests remain authoritative for the installed engine:

- [HttpServer module](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/HttpServer) and [IHttpRouter](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/HttpServer/IHttpRouter)
- [FScopedTransaction](https://dev.epicgames.com/documentation/unreal-engine/API/Editor/UnrealEd/FScopedTransaction)
- [FAssetRegistryModule](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AssetRegistry/FAssetRegistryModule)
- [FKismetEditorUtilities](https://dev.epicgames.com/documentation/unreal-engine/API/Editor/UnrealEd/FKismetEditorUtilities)
- [FBlueprintEditorUtils](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/UnrealEd/FBlueprintEditorUtils)
- [USubobjectDataSubsystem](https://dev.epicgames.com/documentation/unreal-engine/API/Editor/SubobjectDataInterface/USubobjectDataSubsystem)
- [UBlueprintNodeSpawner](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintGraph/UBlueprintNodeSpawner) and [UEdGraphSchema_K2](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Editor/BlueprintGraph/UEdGraphSchema_K2)
- [FCompilerResultsLog](https://dev.epicgames.com/documentation/unreal-engine/API/Editor/UnrealEd/FCompilerResultsLog)
- [UPackage saving](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/CoreUObject/UPackage)
