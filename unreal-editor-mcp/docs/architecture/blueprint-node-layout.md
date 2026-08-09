# Blueprint node layout

## Ownership

`UnrealMCPBlueprintNodeLayout` owns deterministic `layered_v1` placement for the changed-node set created by `blueprint_block_replace`. It receives the completed scratch graph and semantic node-key map from `FUnrealMCPBlueprintBlockReplacementService`; it does not discover assets, resolve actions, compile, transact, save, or move unrelated nodes.

## Dependency direction

The replacement service creates nodes, defaults, direct links, external links, and automatic conversion nodes in its isolated scratch Blueprint before invoking layout. The layout component derives a bounded directed graph, computes and applies scratch positions, and returns an exact key-to-position result plus fingerprint and bounds. The service compiles that candidate and applies the same resolved positions during its one live transaction. Layout never depends on Slate, an open Blueprint editor, selection state, or `SGraphPanel`.

## Policy and invariants

- `$entry` remains at its inspected position and is the fixed origin. Result/tunnel, body, and inserted conversion nodes are the movable set.
- `layered_v1` uses deterministic strongly connected components, stable layered assignment, fixed crossing-reduction sweeps, integer extent estimates, grid snapping, and bounded obstacle search. Semantic keys break every ordering tie.
- Execution links are weighted ahead of data links. Cycles expand as stable horizontal component strips; branches, joins, pure dependencies, disconnected components, and conversion nodes remain bounded.
- Untouched nodes and comments are immutable obstacles. The smallest comment containing `$entry` becomes a fixed container; layouts that cannot fit without crossing its bounds or another fixed obstacle reject in scratch.
- Node, edge, iteration, collision-probe, coordinate, work-unit, and elapsed-time limits reject before live mutation. Equivalent inputs produce the same positions and layout fingerprint.

## Verification

`UnrealMCP.NodeLayout.DeterministicChangedNodes` covers branches, joins, cycles, conversion keys, comments, fixed obstacles, repeat determinism, and resolved-position replay. Function, macro, custom-event, and native-event replacement use `layered_v1` in the production cross-process workflow, which also covers lost-response replay, compile/save, restart, external links, and preserved unrelated content.
