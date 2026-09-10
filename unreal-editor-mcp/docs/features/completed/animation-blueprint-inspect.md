---
feature_id: animation-blueprint-inspect
status: completed
depends_on: []
released_in: "0.38.0"
---

# `animation-blueprint-inspect` — Animation Blueprint graph inspection

**Outcome:** Discover and inspect authored Animation Blueprint graphs through `blueprint_inspect` without granting animation mutation authority.

**Status:** Completed in 0.38.0 with Windows verification. macOS verification remains preferred follow-up work.

**Depends on:** None.

## Implementation

- [x] Publish the base inspection-only `animation` family and `animation_blueprint_inspection` capability; keep all mutation and catalog operations excluded.
- [x] Reuse bounded discovery, exact inspection, K2 records, persistent identities, inherited ownership, pages, and stale-safe cursors.
- [x] Traverse nested animation graphs with deduplication and bounded work. Distinguish AnimGraphs/layers, state machines, states, and transition/conduit rules. Expose graph ownership, node-to-graph relationships, and direct animation assets.
- [x] Fingerprint nested structure, relationships, animation asset references, pin types/directions, defaults, and connections without compilation, saving, runtime evaluation, or editor selection changes.
- [x] Update user guidance, executable examples, and inspector/family contracts. General animation-node configuration, asset overrides, and runtime animation state remain excluded.

## Verification

- [x] `UnrealMCP.Animation.GraphInspection` covers a saved template Blueprint, all graph kinds, links, exact filters, missing identities, pagination, stale nested-node cursors, mutation rejection, dirty/compile-state preservation, cyclic traversal, and graph limits.
- [x] `UnrealMCP.Animation.AnimationLiveFixture` produces the disposable persisted fixture.
- [x] Complete Windows adaptive and forced-unity builds, full Python/native suites, documentation lint, and binary packaging.
- [x] Run `python scripts/run_headless_integration.py --animation-only` for unloaded discovery, exact filtered/paged records, mutation rejection, unchanged saved bytes, and exact identity/snapshot read-back across two editor processes.

[Back to roadmap](../../../ROADMAP.md) · [Inspector architecture](../../architecture/blueprint-inspector.md)
