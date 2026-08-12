---
feature_id: gas-ability-blueprints-inspect
status: completed
depends_on:
  - companion-plugins
released_in: "0.30.0"
---

# `gas-ability-blueprints-inspect` — Gameplay Ability Blueprint inspection

**Outcome:** Agents can discover and inspect existing `UGameplayAbility`-derived Blueprint assets through an optional editor-only companion plugin without changing the asset or adding a Gameplay Ability System dependency to the base Unreal MCP plugin.

**Implementation status:** Completed in 0.30.0 with `UnrealMCPGAS` 0.1.0 on companion API v1. Windows passed Python/static contracts, adaptive and forced-unity editor builds, the full native Automation suite, full headless production-socket integration, and separate base/GAS packaging. The supported UAT package compiled the GAS implementation outside unity; macOS verification remains preferred follow-up work and Linux is out of scope.

The native companion and API v1 registration remain installed, but 0.36.0 removed the shared model-facing Blueprint inspection route. GAS records are dormant until a redesigned companion read contract is approved and are not routed into `asset-inspect-core`.

**Depends on:**

- [`companion-plugins`](companion-plugins.md)

### Companion and version contract

- `plugin/UnrealMCPGAS/` is a separately packaged editor-only plugin and owns all direct Engine Gameplay Ability System, `GameplayAbilities`, `GameplayTags`, and `GameplayTasks` dependencies. `UnrealMCP` remains GAS-free and fully usable when the companion or Engine dependency is absent.
- The companion reuses the base extension registry, authentication, Game-thread dispatch, Blueprint inspector, snapshots, errors, limits, and shutdown. It adds no listener, credential, Python package, runtime download, or deployment-helper behavior.
- `UnrealMCPGAS` has independent semantic version 0.1.0. Base, fixture, and GAS descriptors/binaries retain exact global `companion_api_version: 1`; no API migration was required. Python admits only exact extension/schema/contribution matches.
- `capabilities` publishes companion semantic/API/schema identity, readiness, limits, read support true, mutation support false, `features.gas_ability_blueprints_inspection`, `features.gas_ability_blueprints_mutation`, and an inspection-only `gameplay_ability` family matrix.

### Read and inspection implementation

- The frozen registry classifies usable native or Blueprint-generated `UGameplayAbility` descendants only while the verified contribution is live. Asset Registry discovery remains bounded and non-loading; exact inspection revalidates the generated class and class default object.
- Ordinary `blueprint_inspect` owns summary, defaults, members, graphs, nodes, pins, diagnostics, references, paging, and the authoritative snapshot. The optional `gameplay_ability` section appends typed policy, tag-container, trigger, and cost/cooldown Gameplay Effect reference records.
- Values report local versus inherited ownership. Tags are sorted; triggers receive 32-character semantic identities; effect references report resolved class and asset identities. Output and scan bounds are explicit, and the full bounded companion fingerprint participates in stale cursor detection even when output is truncated.
- The feature adds no model-facing GAS tool, creation, compilation, saving, default/member/graph mutation, or action-catalog contribution. Attribute Sets, Gameplay Effect authoring, Gameplay Cues, Ability System Component setup, input binding, project GAS configuration, runtime granting/activation, PIE, and arbitrary Ability Task/C++ generation remain outside scope.

### Verification

- Python catalog tests prove exact ready/missing/mismatched admission and no mutation schema. Release contracts prove version/API/schema consistency, GAS-free base dependencies, bounded typed fields, and packaging selection.
- `UnrealMCP.Companions.BlueprintFamilyInspectionIntegration` proves the generic standard-inspector seam, combined snapshots, nested capabilities, and dirtiness preservation. `UnrealMCP.GAS.AbilityBlueprintInspection` creates and compiles a representative ability, exercises local policy and trigger data, reads all typed sections twice, and proves deterministic non-mutating fingerprints.
- Windows adaptive and forced-unity builds, full native Automation and Python suites, base-absent/GAS-disabled capability behavior, full production-socket integration, and separate supported UAT base/GAS packages passed. The UAT GAS package compiled `UnrealMCPGASModule.cpp` as an excluded non-unity translation unit. An additional strict no-PCH run also compiled that GAS unit but stopped on pre-existing base-plugin IWYU failures, so a strict package is not claimed as passing. macOS remains in the roadmap platform backlog.

### Documentation and completion gate

- The [Gameplay Ability user guide](../../user/gameplay-ability-blueprints.md), [architecture component](../../architecture/gas-ability-inspection.md), [wire contracts](../../types/gas-ability-inspection/index.md), companion guide, inspector/family references, packaging help, README, history, and roadmap describe the released behavior.
- The feature is complete after mandatory Windows verification under the repository workflow. Preferred macOS native verification is recorded separately rather than contradicting the shared non-blocking platform policy.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
