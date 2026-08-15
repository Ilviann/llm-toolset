---
feature_id: asset-inspect-animation
status: completed
depends_on:
  - asset-inspect-core
  - native-domain-modules
released_in: "0.51.0"
---

# `asset-inspect-animation` — Animation Blueprint semantic inspection

**Outcome:** The established `asset_inspect` tool analyzes Animation Blueprint lifecycle logic, pose algorithms, layers, state machines, states, conduits, aliases, and transitions without returning animation media or live pose state.

**Depends on:**

- [`asset-inspect-core`](asset-inspect-core.md)
- [`native-domain-modules`](native-domain-modules.md)

## Delivered contract

- Classifies regular, template, and Animation Layer Interface assets and composes their animation records over the common Blueprint identity, members, declarations, and K2 graph semantics.
- Publishes target skeleton, root-motion, threaded-update, linked-layer, sync-group, compiled-feature, implemented-interface, and parent-asset-override semantics where meaningful.
- Exposes pose graphs and layer signatures, plus state-machine topology, entry state, states, conduits, aliases, transition policies, transition-rule graphs, and custom transition blends through stable selectors.
- Keeps each selected pose, state, conduit, transition-rule, or blend graph atomic. Complete output is the default; bounded partial output follows pose flow toward the selected result node and is explicitly marked incomplete.
- Adds normalized animation-node kinds, referenced animation assets, cached-pose names, and linked-layer references without exposing compiler-generated layout.
- Animation Layer Interfaces expose declarations and signatures only, not implementation node bodies.
- Excludes animation frames, pose samples, curves, meshes, thumbnails, live skeletal components, active states, weights, montages, proxies, debug poses, and referenced-asset traversal.

## Verification

- Native Automation covers regular/template/interface modes, pose graphs, state-machine topology, transitions, deterministic selection, exclusions, and dirty-state preservation.
- Python contracts verify leaf-module ownership, isolated `AnimGraph` linkage, classifier routes, public graph reuse, capabilities, and packaging composition.
- Windows UE 5.8 builds, headless production-socket inspection, restart determinism, and base packaging form the release gate. macOS follow-up passed on 2026-08-15 through focused native and production-socket restart coverage, all three editor build modes, and universal base packaging.

[Back to roadmap](../../../ROADMAP.md) · [Animation Blueprint contract](../../types/asset-inspection/asset-types/animation-blueprint.md)
