---
feature_id: gas-gameplay-effects-inspect
status: completed
depends_on:
  - gas-ability-blueprints-inspect
released_in: "0.31.0"
---

# `gas-gameplay-effects-inspect` — Gameplay Effect inspection

**Outcome:** Agents can discover and inspect existing `UGameplayEffect`-derived data-only Blueprint assets through the independently versioned `UnrealMCPGAS` companion without changing the asset or package.

**Implementation status:** Completed in 0.31.0 with `UnrealMCPGAS` 0.2.0 on unchanged companion API v1 and schema revision 1. Mandatory Windows verification covers Python/static contracts, adaptive/forced-unity/non-unity builds, native Automation, production-socket headless integration, and separate base/GAS packaging. macOS remains preferred non-blocking follow-up work; Linux is out of scope.

The native companion and API v1 registration remain installed, but 0.36.0 removed the shared model-facing Blueprint inspection route. Gameplay Effect records are dormant until a redesigned companion read contract is approved and are not routed into `asset-inspect-core`.

**Depends on:**

- [`gas-ability-blueprints-inspect`](gas-ability-blueprints-inspect.md)

### Read and inspection implementation

- The GAS companion registers a second inspection-only contribution for native and Blueprint-generated `UGameplayEffect` descendants. It reuses the base registry, authenticated listener, Game-thread dispatch, Blueprint inspector, pagination, snapshots, errors, and capability composition; the base plugin remains GAS-free.
- Ordinary `blueprint_inspect` discovery publishes the `gameplay_effect` family. Exact inspection selects `gameplay_effect` and receives eleven typed section records for duration/period, modifiers, executions, stacking/overflow, cues, tags, granted abilities, additional effects, requirements, components, and cross-field relationships.
- Magnitudes are limited to scalable float, attribute based, custom calculation class, and set by caller. Public Gameplay Effect Component subclasses are explicitly allowlisted. Unknown layouts and unresolved class, attribute, tag, curve, or asset references remain typed and explicit.
- Records preserve local/inherited ownership, stable nested identities, duplicate state, deterministic ordering, collection bounds, and bounded cycle-aware effect-chain traversal. The complete companion fingerprint joins ordinary Blueprint data in one authoritative snapshot.
- Capabilities publish `gas_gameplay_effects_inspection: true`, `gas_gameplay_effects_mutation: false`, and an inspection-only family matrix. Component/member/graph/action/create/compile/save/default-edit surfaces reject without mutation.
- Runtime Gameplay Effect Specs and application, Ability System Component mutation, Attribute Set and Gameplay Cue authoring, supplied calculations, arbitrary component authoring, and C++ generation remain excluded.

### Verification

- Native focused tests cover all four supported magnitude forms, typed component and reference output, cyclic chain reporting, deterministic repeated fingerprints, dirtiness preservation, and restart-persistent Gameplay Effect plus Gameplay Ability cost-reference fixtures.
- Python catalog and release contracts cover exact native/Python/API/schema admission, two GAS contributions, GAS-free base dependencies, the eleven fixed typed sections, stable bounds, and absence of mutation registration.
- Headless production-socket verification inspects the saved effect twice, compares exact records and snapshots, resolves the Gameplay Ability cost reference, rejects compile/save attempts, and proves unchanged read-back.
- Windows adaptive, forced-unity, and non-unity builds, full native and Python suites, supported UAT base/GAS packages, and full headless integration passed. macOS remains in the roadmap platform backlog.

### Documentation and completion gate

- The [Gameplay Effect user guide](../../user/gameplay-effects.md), [architecture component](../../architecture/gas-gameplay-effect-inspection.md), [wire contracts](../../types/gas-gameplay-effect-inspection/index.md), companion/inspector/family references, README, history, and roadmap describe the released behavior.
- Completion retains one `UnrealMCPGAS` distribution and unchanged companion API v1. Inspection and mutation capabilities remain distinct so a future authoring release can be added without overstating this read-only contract.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
