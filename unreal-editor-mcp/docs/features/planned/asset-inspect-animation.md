---
feature_id: asset-inspect-animation
status: planned
depends_on:
  - asset-inspect-core
released_in: null
---

# `asset-inspect-animation` — Animation Blueprint semantic inspection

**Outcome:** The established `asset_inspect` tool can analyze Animation Blueprint lifecycle logic, pose algorithms, layers, state machines, states and transitions without returning animation media or live pose state.

**Depends on:**

- [`asset-inspect-core`](asset-inspect-core.md)

### Family scope

- Add distinct regular, template, and Animation Layer Interface classifications according to the accepted [Animation Blueprint contract](../../types/asset-inspection/asset-types/animation-blueprint.md).
- Reuse core Blueprint members and K2 graph semantics, then add animation settings, pose graphs, linked layers, parent overrides, sync groups, state-machine topology, state and conduit graphs, transition rules, and custom transition blends.
- Keep every selected pose, state, conduit, transition-rule, or blend graph atomic and apply the common complete/partial graph contract with output-oriented traversal where appropriate.
- Exclude animation frames, pose samples, curves, meshes, thumbnails, live skeletal components, active states, weights, montages, proxies, and debug poses.

### Verification and completion gate

- Test regular/template/interface assets, inheritance and parent overrides, pose/data links, layers, state machines, conduits, aliases, transitions, custom blends, deterministic selector identities, normal/verbose output, oversized coherent slices, media exclusion, and read-only preservation.
- Run the complete core regression suite plus mandatory Windows AnimGraph compilation, native Automation, headless, production-socket, and packaging verification.
- Complete only when all advertised animation structures are semantically connected, graph completeness is explicit, no compiler-layout or media/live-state data leaks, and capabilities match live dispatch.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
