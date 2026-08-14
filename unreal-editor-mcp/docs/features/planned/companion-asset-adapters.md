---
feature_id: companion-asset-adapters
status: planned
depends_on:
  - companion-api-v2
  - commonui-assets-inspect
  - gas-ability-blueprints-inspect
  - gas-gameplay-effects-inspect
released_in: null
---

# `companion-asset-adapters` — Unified companion asset integration

**Outcome:** Approved companion families participate in the common asset inspection and authoring infrastructure while unavailable companions expose neither records nor mutations.

**Depends on:**

- [`companion-api-v2`](../completed/companion-api-v2.md)
- [`commonui-assets-inspect`](../completed/commonui-assets-inspect.md)
- [`gas-ability-blueprints-inspect`](../completed/gas-ability-blueprints-inspect.md)
- [`gas-gameplay-effects-inspect`](../completed/gas-gameplay-effects-inspect.md)

### Implementation

- Migrate released GAS Gameplay Ability, GAS Gameplay Effect, and CommonUI collectors from dormant v1 registrations into v2 inspection adapters routed through `asset_inspect`.
- Register the common v2 creation/edit execution seams but advertise no family mutation until its owning authoring feature is complete and live.
- Compose companion blocks and selector namespaces deterministically without replacing base identity, snapshot, paging, selector, error, or limit fields.
- Keep base-only neutral identity available when a companion or required Engine plugin is absent, disabled, mismatched, stale, unloaded, or rejected.

### Verification and completion gate

- Test cumulative base/companion inspection, exact YAML, selector collisions, snapshots, repeated read-back, disabled and mismatched states, readonly filtering, restart, and unchanged package/Undo state.
- Run complete base, GAS, CommonUI, Python, native Automation, headless, build, deployment, and packaging gates.
- Complete only when the existing companion families are available through the unified facade and no unavailable state leaks inspection or authoring capability.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
