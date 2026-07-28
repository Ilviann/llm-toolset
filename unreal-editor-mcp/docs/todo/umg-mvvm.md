# `umg-mvvm` — UMG ViewModels and View Bindings

**Outcome:** Agents can create and edit Blueprint ViewModels, attach them to Widget Blueprints, and author validated event-driven UMG View Bindings through an optional lockstep-versioned companion without making Unreal's UMG Viewmodel plugin a dependency of the base Unreal MCP plugin.

**Depends on:**

- [`umg-authoring`](umg-authoring.md)

### Companion and version contract

- Add a separate editor-only `UnrealMCPMVVM` companion plugin that owns all direct dependencies on the Engine's `ModelViewViewModel` plugin and the public `ModelViewViewModel`, `ModelViewViewModelBlueprint`, and `ModelViewViewModelEditor` modules actually required by compiled probes and native behavior. Do not depend on `ModelViewViewModelDebugger` in production code.
- Keep `UnrealMCP` independent of UMG Viewmodel headers, modules, plugin enablement, and transitive Engine-plugin dependencies. The base plugin must build, package, load, and retain Widget Blueprint creation, UMG authoring, legacy property bindings, and Designer events when `UnrealMCPMVVM` or the Engine plugin is absent.
- Add or reuse one narrow base-owned extension interface rather than another listener or credential. The companion may register only its ViewModel family policy, FieldNotify/member handlers, Widget MVVM handlers, inspection contributors, and bounded capability data; it must reuse base authentication, dispatch, ledgers, errors, snapshots, compilation, saving, and shutdown.
- Release `UnrealMCPMVVM` in exact lockstep with `UnrealMCP`. Give both descriptors the same numeric `Version` and string `VersionName`; set the companion's required `UnrealMCP` plugin reference `RequestedVersion` to that exact numeric version; package them from one source state in the same release bundle.
- Reference the Engine plugin by descriptor name `ModelViewViewModel` without an `UnrealMCP`-defined `RequestedVersion`; its compatibility is established by building and testing against the exact supported Unreal Engine distribution. Never download, install, or enable it at runtime.
- Extend release-contract tests to compare Python, native, base descriptor, and companion descriptor versions and the companion's requested base version. Publish companion presence, version, readiness, Engine-plugin state, supported initialization modes, binding modes, conversion policy, and effective limits through `capabilities`; fail closed and expose no MVVM mutations when any required component is absent or mismatched.

### ViewModel Blueprint authoring

- Add an explicit `viewmodel` Blueprint family only while the verified companion capability is live. Accept native or Blueprint-generated parents only when they resolve to usable `UMVVMViewModelBase` subclasses, or another explicitly supported `INotifyFieldValueChanged` base proven safe by public APIs, and pass established class, package, mount, compilation, and stale-state policies.
- Reuse `blueprint_create`, `blueprint_inspect`, `blueprint_member_edit`, `blueprint_action_catalog`, `blueprint_graph_edit`, `blueprint_compile`, and `blueprint_save`. Publish an exact non-Actor family capability matrix and reject Actor components, Actor replication settings, and unrelated specialized-family operations.
- Inspect and edit supported Blueprint variables and functions with FieldNotify state, access, getter/setter availability, dependent FieldNotify relationships, types, defaults, stable member identities, and authoritative snapshots. Do not infer notification support from naming conventions or expose arbitrary metadata mutation.
- Validate FieldNotify dependencies as a bounded directed graph. Reject missing, incompatible, inaccessible, duplicate, cyclic, stale, or excessive relationships and preserve all unrelated members, metadata, graph logic, notifications, and prior dirty state.

### Widget ViewModel and binding authoring

- Extend `blueprint_inspect` for Widget Blueprints with bounded MVVM ViewModel-context and View Binding sections. Distinguish MVVM records from legacy polling property bindings and Designer events, give every context, binding, path segment, and conversion record a stable identity, and include them in the authoritative Widget Blueprint snapshot and preservation fingerprint.
- Extend `widget_tree_edit` with typed operations to add, update, rename where Unreal supports it, and remove one ViewModel context; configure supported Create Instance, Manual, Global Viewmodel Collection, and Property Path initialization; and add, update, enable/disable, or remove one View Binding.
- Resolve ViewModel classes, widget properties/functions, FieldNotify sources, destinations, conversion functions, and conversion arguments against live public MVVM editor APIs. Accept inspected stable identities and bounded typed path segments rather than unvalidated dotted property strings or arbitrary reflection paths.
- Support only live-valid One Time to Widget, One Way to Widget, One Way to ViewModel, and Two Way directions and their compatible update policies. Verify readable/writable access, notification requirements, value types, source/destination direction, initialization availability, duplicate targets, conversion signatures, and Blueprint compilation before commit.
- Preserve unrelated widget hierarchy, defaults, layout, styles, legacy bindings, Designer events, ViewModel contexts, View Bindings, conversions, graph logic, animations, and generated MVVM extensions. Use one transaction per accepted mutation, exact postcondition verification, retained-operation reconciliation, bounded diagnostics, and explicit restoration after unexpected failure.
- Keep runtime ViewModel instances, global collection mutation, gameplay-object access, arbitrary property paths, unrestricted converters, dynamic binding execution, ViewModel preview plugins, debugger integration, widget animation, and project-wide MVVM settings outside this feature.

### Verification

- Prove the base plugin builds, packages, starts, and passes its complete Widget/UMG suite with the companion absent and with the Engine UMG Viewmodel plugin disabled.
- Build and package the companion with normal, forced-unity, and non-unity editor builds. Test missing base, missing or disabled Engine plugin, mismatched numeric/display versions, stale binaries, unavailable public editor modules, and unsupported Unreal builds without partial family or handler registration.
- Create representative Blueprint ViewModels with readable, writable, derived, and converted FieldNotify values. Compile, save, restart, and verify exact members, dependencies, generated behavior, and notification metadata.
- Attach multiple ViewModels to representative HUD, menu, and reusable user-widget Blueprints using each supported initialization mode. Author each supported binding direction, compatible conversion functions and arguments, enabled/disabled bindings, and nested ViewModel paths; compile, save, restart, and verify exact read-back.
- Test invalid classes, inaccessible members, non-notifying sources, read/write direction failures, incompatible conversions, broken or cyclic paths, duplicate target bindings, stale identities and snapshots, collection/depth limits, compile failure, rollback, undo/redo, timeout, replay, and lost-response recovery.
- Prove every accepted and rejected operation preserves fingerprints for unrelated MVVM records, legacy bindings, Designer events, widget trees, graphs, animations, and package dirtiness. Run the complete base and MVVM companion suites natively on macOS and Windows against the supported Unreal Engine build.

### Documentation and completion gate

- Document companion installation and enablement, exact lockstep versioning, capability detection, ViewModel Blueprint and FieldNotify workflows, initialization modes, binding directions, conversions, stable paths, performance behavior, limits, exclusions, recovery, and focused HUD/menu examples.
- Document that UMG Viewmodel is an optional Beta Engine plugin whose compatibility must be verified per supported Unreal build, while legacy bindings remain available through base Unreal MCP.
- Complete the feature only when absent or mismatched companions cannot expose or execute MVVM operations, the base Widget/UMG workflow remains fully usable without the Engine plugin, and representative ViewModels and Widget View Bindings can be created, edited, compiled, saved, restarted, and read back without altering unrelated content on both native platforms.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
