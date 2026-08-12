---
feature_id: umg-mvvm
status: planned
depends_on:
  - umg-mvvm-inspect
  - asset-authoring-kernel
  - companion-api-v2
released_in: null
---

# `umg-mvvm` — UMG ViewModel and View Binding authoring

**Outcome:** Agents can create and edit Blueprint ViewModels, attach them to Widget Blueprints, and author validated event-driven UMG View Bindings through the optional `UnrealMCPMVVM` companion after bounded inspection support is available.

**Depends on:**

- [`umg-mvvm-inspect`](umg-mvvm-inspect.md)
- [`asset-authoring-kernel`](../completed/asset-authoring-kernel.md)
- [`companion-api-v2`](companion-api-v2.md)

### ViewModel Blueprint authoring

- Reuse the independently versioned optional `UnrealMCPMVVM` companion, strict companion API v2 requirement, family registry, shared authoring kernel, authenticated bridge, Game-thread dispatch, operation ledger, errors, limits, packaging, and inspection capability established by `umg-mvvm-inspect`. Do not add UMG Viewmodel dependencies to `UnrealMCP` or create another listener, credential, Python package, or extension API.
- Add the `viewmodel` mutation capability only when the companion and prerequisite inspection capability are verified live. Accept native or Blueprint-generated parents for creation and updates only when they resolve to usable `UMVVMViewModelBase` subclasses, or another explicitly supported `INotifyFieldValueChanged` base proven safe by public APIs, and pass established class, package, mount, compilation, and stale-state policies.
- Reuse `blueprint_create`, `blueprint_member_edit`, `blueprint_action_catalog`, `blueprint_graph_edit`, `blueprint_compile`, and `blueprint_save`; obtain stale-safe read-back through the redesigned MVVM inspection contract built on `asset-inspect-umg`. Preserve the exact non-Actor family capability matrix and reject Actor components, Actor replication settings, and unrelated specialized-family operations.
- Inspect and edit supported Blueprint variables and functions with FieldNotify state, access, getter and setter availability, dependent FieldNotify relationships, types, defaults, and stable member identities. Do not infer notification support from naming conventions or expose arbitrary metadata mutation.
- Validate FieldNotify dependencies as a bounded directed graph. Reject missing, incompatible, inaccessible, duplicate, cyclic, stale, or excessive relationships and preserve all unrelated members, metadata, graph logic, notifications, generated data, and prior dirty state.

### Widget ViewModel and binding authoring

- Extend `widget_tree_edit` with typed operations to add, update, rename where Unreal supports it, and remove one ViewModel context; configure supported Create Instance, Manual, Global Viewmodel Collection, and Property Path initialization; and add, update, enable or disable, or remove one View Binding.
- Resolve ViewModel classes, widget properties and functions, FieldNotify sources, destinations, conversion functions, and conversion arguments against live public MVVM editor APIs. Accept inspected stable identities and bounded typed path segments rather than unvalidated dotted property strings or arbitrary reflection paths.
- Support only live-valid One Time to Widget, One Way to Widget, One Way to ViewModel, and Two Way directions and their compatible update policies. Verify readable and writable access, notification requirements, value types, source and destination direction, initialization availability, duplicate targets, conversion signatures, and Blueprint compilation before commit.
- Preserve unrelated widget hierarchy, defaults, layout, styles, legacy bindings, Designer events, ViewModel contexts, View Bindings, conversions, graph logic, animations, and generated MVVM extensions. Use one transaction per accepted mutation, authoritative prerequisite snapshots, stable record identities, exact postcondition verification, retained-operation reconciliation, bounded diagnostics, and explicit restoration after unexpected failure.
- Keep runtime ViewModel instances, global collection mutation, gameplay-object access, arbitrary property paths, unrestricted converters, dynamic binding execution, ViewModel preview plugins, debugger integration, widget animation, and project-wide MVVM settings outside this feature.

### Verification

- Create representative Blueprint ViewModels with readable, writable, derived, and converted FieldNotify values. Compile, save, restart, and verify exact members, dependencies, generated behavior, and notification metadata through the prerequisite inspection contract.
- Attach multiple ViewModels to representative HUD, menu, and reusable user-widget Blueprints using each supported initialization mode. Author each supported binding direction, compatible conversion functions and arguments, enabled and disabled bindings, and nested ViewModel paths; compile, save, restart, and verify exact read-back.
- Test invalid classes, inaccessible members, non-notifying sources, read and write direction failures, incompatible conversions, broken or cyclic paths, duplicate target bindings, stale identities and snapshots, collection and depth limits, compile failure, rollback, undo and redo, timeout, replay, and lost-response recovery.
- Test absent, inspection-only, mismatched, disabled, stale, and unsupported companion states without partially registering or executing mutation handlers. Verify mutation capability registration and removal across editor restart and plugin enablement changes.
- Prove every accepted and rejected operation preserves fingerprints for unrelated MVVM records, legacy bindings, Designer events, widget trees, graphs, animations, package dirtiness, and base-plugin behavior. Run the complete base and MVVM companion suites natively on Windows against the supported Unreal Engine build. Repeat on macOS when available as non-blocking follow-up.

### Documentation and completion gate

- Document the additional mutation capability, ViewModel Blueprint and FieldNotify creation and update workflows, ViewModel-context operations, binding directions, conversions, snapshot and stable-identity requirements, compilation and saving, limits, exclusions, recovery, and focused HUD and menu examples. Link to the prerequisite inspection guide instead of repeating its read contract.
- Update companion installation, packaging, independent semantic-version, and companion API documentation only where mutation support changes them; retain one shared `UnrealMCPMVVM` distribution and never download or enable dependencies at runtime.
- Complete the feature only when representative ViewModels and Widget View Bindings can be created, edited, compiled, saved, restarted, and read back without altering unrelated content, inspection-only or mismatched companions cannot expose or execute mutation operations, and the complete base and MVVM suites pass on both native platforms.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
