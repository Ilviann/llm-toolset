---
feature_id: node-layout
status: completed
depends_on:
  - event-macro-replace
released_in: "0.34.0"
---

# `node-layout` — Deterministic changed-node layout

**Outcome:** `blueprint_block_replace` can lay out changed nodes predictably without moving or otherwise altering untouched Blueprint content.

**Depends on:**

- [`event-macro-replace`](event-macro-replace.md)

### Implementation

- Added the exact `layout: {"policy":"layered_v1"}` alternative to explicit positions for function, macro, custom-event, and native-event replacement plans.
- The bounded native planner derives execution-weighted dependencies from the compiled scratch graph, collapses cycles into strongly connected components, assigns layers, performs stable barycenter sweeps, expands cycles predictably, and resolves collisions on a fixed integer grid.
- `$entry` remains anchored. Only changed entry/result/body/conversion nodes move; unrelated nodes and comments are fixed obstacles. The smallest comment containing the entry acts as the changed block's fixed container.
- Scratch-resolved positions and the layout fingerprint are replayed exactly in the live transaction. An untouched-graph fingerprint verifies positions, comment geometry, structural pins, defaults, and links between unchanged nodes before commit.
- Published node, edge, iteration, collision-probe, work, coordinate, and Game-thread-time limits reject unbounded work before live mutation. Explicit caller-supplied positions remain supported.

### Verification

- `UnrealMCP.NodeLayout.DeterministicChangedNodes` covers branches, joins, cycles, conversion keys, containing comments, fixed obstacles, repeated planning, and exact resolved-position replay.
- Python contract tests cover all four automatic-layout request variants and reject mixed layout/position shapes.
- The Windows UE 5.8 production lifecycle replaces a function, macro, custom-event handler, and native-event handler through `layered_v1`, then verifies lost-response recovery, replay, compile/save, restart, external links, stable layout fingerprints, and unrelated content.
- Windows adaptive build, forced-unity build, full native Automation, packaging, Python tests, and documentation lint are release gates. macOS verification remains a non-blocking roadmap follow-up; Linux is outside the current support and verification scope.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
