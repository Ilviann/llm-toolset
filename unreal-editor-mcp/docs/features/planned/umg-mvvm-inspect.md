---
feature_id: umg-mvvm-inspect
status: planned
depends_on:
  - umg-authoring
  - asset-inspect-umg
  - companion-asset-adapters
released_in: null
---

# `umg-mvvm-inspect` — UMG ViewModel and View Binding inspection

**Outcome:** `asset_inspect` can discover and inspect existing Blueprint ViewModels, Widget Blueprint ViewModel contexts, and UMG View Bindings through an optional editor-only companion plugin without changing an asset or making Unreal's UMG Viewmodel plugin a dependency of the base Unreal MCP plugin.

**Depends on:**

- [`umg-authoring`](../completed/umg-authoring.md)
- [`asset-inspect-umg`](../completed/asset-inspect-umg.md)
- [`companion-asset-adapters`](../completed/companion-asset-adapters.md)

### Companion and version contract

- Add a separate editor-only `UnrealMCPMVVM` companion plugin that owns all direct dependencies on the Engine's `ModelViewViewModel` plugin and the public `ModelViewViewModel`, `ModelViewViewModelBlueprint`, and `ModelViewViewModelEditor` modules actually required by compiled probes and native behavior. Do not depend on `ModelViewViewModelDebugger` in production code.
- Keep `UnrealMCP` independent of UMG Viewmodel headers, modules, plugin enablement, and transitive Engine-plugin dependencies. The base plugin must build, package, load, and retain Widget Blueprint creation, UMG authoring, legacy property bindings, and Designer events when `UnrealMCPMVVM` or the Engine plugin is absent.
- Reuse the typed asset-family registry, companion lifecycle, and composed `asset_inspect` adapter seam established by `companion-asset-adapters` rather than adding another listener, credential, or model-facing tool. The companion may register only its ViewModel family policy, FieldNotify and inspection adapters, Widget MVVM selectors, and bounded capability data; it must reuse base authentication, dispatch, safe-YAML rendering, errors, snapshots, limits, and shutdown.
- Version `UnrealMCPMVVM` independently from `UnrealMCP` under the shared semantic-version policy. Require both descriptors to declare the same global `companion_api_version` and require the compiled base and companion constants to match each other and their descriptors before registration; do not pin the companion's required `UnrealMCP` plugin reference to the base semantic version.
- Reference the Engine plugin by descriptor name `ModelViewViewModel` without an `UnrealMCP`-defined `RequestedVersion`; establish compatibility by building and testing against the exact supported Unreal Engine distribution. Never download, install, or enable it at runtime.
- Extend release-contract tests to validate Python/base version pairing, each plugin's internal semantic-version sources, descriptor and compiled companion API values, and extension-schema compatibility. Publish companion semantic version, `companion_api_version`, readiness, Engine-plugin state, inspection support, supported initialization and binding modes, conversion policy, and effective limits through `capabilities`; fail closed and expose no MVVM family when any required component or compatibility value is absent or mismatched.

### ViewModel Blueprint inspection

- Add an explicit inspection-only `viewmodel` Blueprint family while the verified companion capability is live. Accept native or Blueprint-generated parents only when they resolve to usable `UMVVMViewModelBase` subclasses, or another explicitly supported `INotifyFieldValueChanged` base proven safe by public APIs, and pass established class, package, mount, compilation, and stale-class policies.
- Extend the semantic UMG facade established by `asset-inspect-umg` for bounded asset summary, defaults, declarations, graphs, diagnostics, references, and one authoritative asset snapshot. Publish an exact non-Actor family capability matrix and reject Actor components, Actor replication settings, creation, default/member/graph mutation, action cataloging, compilation, saving, and unrelated specialized-family operations.
- Add bounded typed inspection for supported Blueprint variables and functions with FieldNotify state, access, getter and setter availability, dependent FieldNotify relationships, types, defaults, inherited versus locally declared state, and stable member identities. Do not infer notification support from naming conventions or expose arbitrary metadata.
- Report the FieldNotify dependency graph with explicit missing, incompatible, inaccessible, duplicate, cyclic, unsupported, stale, or truncated relationships without repairing or normalizing the asset.

### Widget ViewModel and binding inspection

- Extend `asset_inspect` for Widget Blueprints with bounded MVVM ViewModel-context and View Binding selectors. Distinguish MVVM records from legacy polling property bindings and Designer events, give every context, binding, path segment, and conversion record a stable identity, and include them in the authoritative Widget Blueprint snapshot and preservation fingerprint.
- Report supported initialization mode, ViewModel class, property path, creation context, binding direction, update policy, enabled state, source and destination paths, conversion function and arguments, inherited or generated state, resolution failures, and truncation without executing a binding or instantiating runtime ViewModels.
- Resolve and describe classes, widget properties and functions, FieldNotify sources, destinations, path segments, and conversion signatures through live public MVVM editor APIs. Return typed unsupported or unresolved records instead of accepting unvalidated dotted paths, using unrestricted reflection, or silently omitting unknown data.
- Keep ViewModel context mutation, View Binding mutation, runtime ViewModel instances, global collection mutation, gameplay-object access, dynamic binding execution, ViewModel preview plugins, debugger integration, and project-wide MVVM settings outside this feature.

### Verification

- Prove the base plugin builds, packages, starts, and passes its complete Widget and UMG suite with the companion absent and with the Engine UMG Viewmodel plugin disabled.
- Build and package the companion with normal, forced-unity, and non-unity editor builds. Test missing base, missing or disabled Engine plugin, missing or mismatched descriptor API values, compiled API mismatch, descriptor-versus-compiled disagreement, stale binaries, inconsistent companion-owned semantic-version sources, independently different valid semantic versions, unavailable public editor modules, and unsupported Unreal builds without partial family or handler registration.
- Inspect representative existing Blueprint ViewModels with readable, writable, derived, and converted FieldNotify values. Cover native and Blueprint-generated parents, inherited and local members, dependency relationships, supported graphs, and notification metadata; restart and verify the same bounded read-back.
- Inspect Widget Blueprints with multiple ViewModels, every supported initialization mode and binding direction, compatible conversions and arguments, enabled and disabled bindings, nested ViewModel paths, legacy bindings, and Designer events; restart and verify exact separation and read-back.
- Test invalid classes, inaccessible members, non-notifying sources, broken or cyclic paths, incompatible conversions, duplicate target bindings, unsupported records, stale identities, pages and snapshots, and collection, depth, scan, time, and response limits.
- Fingerprint asset content, generated data, package dirtiness, and Undo history before and after every success and rejection. Prove inspection never creates, compiles, saves, normalizes, or changes ViewModels, Widget Blueprints, bindings, graphs, or referenced assets and that no MVVM mutation capability is advertised or accepted.
- Run focused public-header probes, native Automation, packaging and version-contract tests, and full cross-process inspection verification on Windows against the supported Unreal Engine build. Repeat on macOS when available as non-blocking follow-up.

### Documentation and completion gate

- Document companion installation and enablement, independent semantic versioning, strict `companion_api_version` equality, capability detection, supported ViewModel and View Binding inspection records, initialization and binding modes, inheritance, stable paths and identities, pagination, limits, exclusions, and focused read-only examples.
- Document that UMG Viewmodel is an optional Beta Engine plugin whose compatibility must be verified per supported Unreal build, while legacy bindings remain available through base Unreal MCP. Document offline source and binary packaging from one release state; never download or enable dependencies at runtime.
- Complete the feature only when absent or mismatched companions cannot expose or execute MVVM inspection, the base Widget and UMG workflow remains fully usable without the Engine plugin, representative existing ViewModels and Widget View Bindings can be read after restart without any asset or package mutation, and the complete Windows base and inspection gates pass. Record remaining applicable macOS verification in the roadmap backlog.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
