---
feature_id: commonui-assets-inspect
status: planned
depends_on:
  - umg-authoring
  - companion-plugins
released_in: null
---

# `commonui-assets-inspect` — Inspect assets with CommonUI plugin dependencies

**Outcome:** Agents can discover and inspect supported assets whose classes or serialized state depend on Unreal's optional `CommonUI` plugin, without changing an asset or adding CommonUI dependencies to the base Unreal MCP plugin.

**Depends on:**

- [`umg-authoring`](../completed/umg-authoring.md)
- [`companion-plugins`](../completed/companion-plugins.md)

**Planning note:** Before implementation, freeze the exact supported asset-family allowlist and typed records against the supported Unreal Engine build, public CommonUI editor APIs, and the executable tool catalog. CommonUI-dependent Widget Blueprints and directly owned supporting asset types are stable scope candidates; unrestricted inspection of every asset that merely references CommonUI is not.

### Project enablement and companion contract

- Add a separately packaged editor-only `UnrealMCPCommonUI` companion plugin that owns every direct dependency on the Engine plugin named `CommonUI` and the public `CommonUI`, `CommonInput`, and `CommonUIEditor` modules actually required by compiled probes and native behavior.
- Keep `UnrealMCP` independent of CommonUI headers, modules, plugin enablement, content, and transitive dependencies. The base plugin must build, package, load, and retain its complete non-CommonUI Blueprint and Widget contract when the companion is absent or CommonUI is missing, disabled, or unloaded.
- Require Unreal's plugin manager to report `CommonUI` effectively enabled for the configured project and require the necessary modules to load successfully. Never install or enable CommonUI, edit the project descriptor, restart the editor, or infer readiness from installed files or stale capabilities.
- Reuse the base-owned companion registry, authenticated bridge, Game-thread dispatch, errors, limits, snapshots, pagination, and shutdown. Version the companion independently while requiring exact descriptor and compiled `companion_api_version` equality and exact extension-schema admission.
- Publish bounded capability data for companion identity, CommonUI enablement and load state, inspection readiness, supported asset families and typed sections, and effective limits. Fail closed and expose no CommonUI family or inspection contribution when any required state is absent, disabled, unloaded, stale, or mismatched.

### Inspection contract

- Extend existing asset, Blueprint, and Widget inspection tools for an explicit allowlist of CommonUI-dependent families; do not add a separate model-facing CommonUI tool or unrestricted reflection surface.
- Report bounded typed CommonUI-owned configuration, nested records, referenced assets and classes, local versus inherited ownership, stable identities, unsupported or unresolved state, truncation, and one authoritative snapshot suitable for later stale-safe mutation.
- Preserve the existing inspection records for ordinary Blueprint and Widget content and include every exposed CommonUI-owned record in preservation fingerprints and stale-page detection.
- Bound discovery, nested collections, dependency traversal, reference resolution, diagnostics, time, retained state, and response size. Do not load arbitrary dependency graphs, execute UI behavior, instantiate runtime widgets, compile, save, normalize, or mutate assets.

### Verification and completion gate

- Test absent, installed-but-disabled, enabled, load-failed, mismatched, stale, and unsupported companion or Engine-plugin states without partial registration. Prove the base plugin remains fully usable in every unavailable state.
- Inspect representative assets for every frozen supported family, including inherited, unresolved, unsupported, duplicate, cyclic-reference, stale-snapshot, and limit cases; restart and verify deterministic read-back.
- Prove every success and rejection preserves asset content, package dirtiness, generated state, and Undo history. Run public-header probes, normal, forced-unity, and non-unity builds, native Automation, packaging, release-contract, and cross-process inspection verification on Windows; record macOS as preferred non-blocking follow-up.
- Document installation and enablement, capability detection, supported families and records, limits, exclusions, stable unavailable reasons, and read-only examples. Complete the feature only when disabled or incompatible states expose no CommonUI inspection, representative assets read back without mutation, and the complete base and companion suites pass on Windows.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
