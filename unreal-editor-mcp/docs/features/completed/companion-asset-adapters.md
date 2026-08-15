---
feature_id: companion-asset-adapters
status: completed
depends_on:
  - companion-api-v2
  - commonui-assets-inspect
  - gas-ability-blueprints-inspect
  - gas-gameplay-effects-inspect
released_in: "0.45.0"
---

# `companion-asset-adapters` — Unified companion asset integration

**Outcome:** Approved companion families participate in the common asset inspection infrastructure while unavailable companions expose neither records nor mutations.

**Implementation status:** Completed in 0.45.0 with `UnrealMCPGAS` 0.3.0 and `UnrealMCPCommonUI` 0.2.0 on unchanged companion API v2 and schema revision 2. Windows passed the Python, native Automation, production-socket lifecycle, adaptive/forced-unity/non-unity build, and independent packaging gates. macOS passed the corresponding follow-up gates on 2026-08-15; Linux is out of scope.

**Depends on:**

- [`companion-api-v2`](companion-api-v2.md)
- [`commonui-assets-inspect`](commonui-assets-inspect.md)
- [`gas-ability-blueprints-inspect`](gas-ability-blueprints-inspect.md)
- [`gas-gameplay-effects-inspect`](gas-gameplay-effects-inspect.md)

### Implementation

- Migrated the released GAS Gameplay Ability, GAS Gameplay Effect, and CommonUI collectors from dormant typed contributions into API-v2 inspection-only asset families routed through `asset_inspect`.
- Registered the common API-v2 creation/edit seams while advertising `create: false` and `edit: false`; the owning authoring features remain responsible for adding mutation adapters and Python schemas.
- Composed companion blocks, selector routes, and fingerprint contributions deterministically over the selected base family. Base identity, request validation, paging policy, errors, limits, and final safe-YAML encoding remain owned by the common service.
- Kept base-only neutral identity available when a companion or required Engine plugin is absent, disabled, mismatched, stale, unloaded, or rejected. Rejected companion declarations never partially enter the common registry.

### Verification evidence

- Python catalog, extension, contract, server, and headless tests cover exact family identities, inspection-only operations, unavailable states, readonly behavior, and unchanged tool schemas.
- Native Automation covers registration/admission, cumulative base/companion inspection, selector routing and collision rejection, deterministic snapshots, repeat reads, and unchanged package and Blueprint state.
- The full Windows production-socket lifecycle verifies Gameplay Ability, Gameplay Effect, and CommonUI root/selector reads and repeatability across the authenticated `asset_inspect` facade.
- Adaptive, true forced-unity, and explicit non-unity UE 5.8 editor builds plus isolated base, GAS, CommonUI, and fixture Win64 packages are release gates.
- macOS follow-up passed on 2026-08-15 through the corresponding native, production-socket restart, three-mode build, and isolated universal-package gates.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
