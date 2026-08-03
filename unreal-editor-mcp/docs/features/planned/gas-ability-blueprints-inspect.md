---
feature_id: gas-ability-blueprints-inspect
status: planned
depends_on:
  - companion-plugins
released_in: null
---

# `gas-ability-blueprints-inspect` — Gameplay Ability Blueprint inspection

**Outcome:** Agents can discover and inspect existing `UGameplayAbility`-derived Blueprint assets through an optional editor-only companion plugin without changing the asset or adding a Gameplay Ability System dependency to the base Unreal MCP plugin.

**Depends on:**

- [`companion-plugins`](../completed/companion-plugins.md)

### Companion and version contract

- Add a separate editor-only `UnrealMCPGAS` companion plugin that owns all direct dependencies on the Engine's Gameplay Ability System plugin and its `GameplayAbilities`, `GameplayTags`, and `GameplayTasks` modules. Add an editor module dependency only for public editor APIs exercised by compiled probes and native behavioral tests.
- Keep `UnrealMCP` independent of Gameplay Ability System headers, modules, and plugin enablement. The base plugin must build, package, load, and retain its complete non-GAS capabilities when `UnrealMCPGAS` or the Engine Gameplay Ability System plugin is absent.
- Reuse the base-owned extension registry and companion lifecycle established by `companion-plugins` rather than adding another listener or credential. The companion may register only its explicit family policies, typed handlers, action contributors, and bounded capability data with the base bridge; it must reuse base authentication, Game-thread dispatch, errors, limits, and shutdown.
- Version `UnrealMCPGAS` independently from `UnrealMCP` under the shared semantic-version policy. Require both descriptors to declare the same global `companion_api_version` and require the compiled base and companion constants to match each other and their descriptors before registration; do not pin the companion's required `UnrealMCP` plugin reference to the base semantic version.
- Extend release-contract tests to validate Python/base version pairing, each plugin's internal semantic-version sources, descriptor and compiled companion API values, and extension-schema compatibility. Publish companion semantic version, `companion_api_version`, readiness, and separate GAS inspection and mutation capabilities through `capabilities`. Fail closed and expose no GAS family when the companion API, schema, base, Python server, Unreal build, or Engine GAS dependency is missing or mismatched.

### Read and inspection implementation

- Add an explicit inspection-only `gameplay_ability` Blueprint family while the verified companion capability is live. Accept native or Blueprint-generated parents only when they resolve to usable `UGameplayAbility` subclasses and pass the established class, package, mount, stale-class, and asset-access policies.
- Reuse `blueprint_inspect` for bounded asset summary, defaults, members, graphs, nodes, pins, diagnostics, and references. Do not publish a separate model-facing GAS tool, create or save assets, compile Blueprints, expose action-catalog additions, or enable any default, member, or graph mutation in this feature.
- Add bounded typed inspection sections for the supported activation, instancing, and network policies; Gameplay Tag and trigger containers; and compatible cost and cooldown Gameplay Effect class references. Report inherited versus locally declared values, resolved reference identities, stable nested identities where names or indexes are insufficient, truncation, and one authoritative asset snapshot.
- Preserve the established graph, node, pin, member, default, reference, diagnostic, pagination, and response limits. Existing Gameplay Ability events, functions, and Ability Task nodes remain ordinary inspected Blueprint graph content; inspection must not synthesize actions or use unrestricted reflection.
- Keep Attribute Set and Gameplay Effect authoring, Gameplay Cue asset authoring, Ability System Component setup on Actors, input binding, project GAS configuration, runtime ability granting or activation, PIE execution, and arbitrary custom Ability Task or C++ generation outside this feature.

### Verification

- Prove the base plugin builds, packages, starts, and passes its complete suite with the companion absent and with the Engine Gameplay Ability System plugin disabled.
- Build and package the companion with normal, forced-unity, and non-unity editor builds. Test missing base, missing GAS dependency, disabled dependency, missing or mismatched descriptor API values, compiled API mismatch, descriptor-versus-compiled disagreement, stale binaries, inconsistent companion-owned semantic-version sources, independently different valid semantic versions, and unsupported Unreal builds without partially registering the family or handlers.
- Inspect representative existing abilities with native and Blueprint-generated parents. Cover every supported policy, tag and trigger form, cost and cooldown reference, inherited and local value, member, graph, existing Gameplay Ability event, and Ability Task node; restart the editor and verify the same bounded read-back.
- Test invalid and stale classes, unsupported GAS asset families, malformed or excessive tag and trigger collections, unresolved references, stale pagination or snapshots, response and scan limits, and capability registration and removal across editor restarts and plugin enablement changes.
- Fingerprint asset content and package dirtiness before and after every success and rejection. Prove inspection never changes graphs, members, defaults, generated classes, referenced assets, package state, or Undo history and that no GAS mutation operation is advertised or accepted.
- Run focused public-header probes, native Automation, packaging and version-contract tests, and full cross-process inspection verification on Windows against the supported Unreal Engine build. Repeat on macOS when available as non-blocking follow-up.

### Documentation and completion gate

- Document companion installation and enablement, independent semantic versioning, strict `companion_api_version` equality, read-versus-mutation capability detection, supported Gameplay Ability inspection sections, inheritance, pagination, limits, exclusions, and focused inspection examples.
- Document offline source and binary packaging of the base and companion from one release state. Never download or enable dependencies at runtime.
- Complete the feature only when a mismatched or absent companion cannot expose or execute GAS inspection, the base plugin remains fully usable without GAS, representative existing Gameplay Ability Blueprints can be read after restart without any asset or package mutation, and the complete base and inspection suites pass on both native platforms.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
