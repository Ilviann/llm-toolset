# `pcg-component-edit` — Level actor PCG Component editing

**Outcome:** Agents can inspect and transactionally configure PCG Components on exact level actors, explicitly generate or clean up their managed output, and save and verify all affected level packages.

**Depends on:**

- [`pcg-graph-authoring`](pcg-graph-authoring.md)
- [`level-edit`](level-edit.md)

### Implementation

- Extend bounded level inspection with PCG Component identity, assigned graph, parameter overrides, seed, activation, generation trigger, partitioning, generation state, managed-output summary, and supported runtime-generation settings.
- Extend level actor editing with typed operations to add supported instance PCG Components, assign or clear an exact PCG Graph, edit supported component settings and graph-parameter overrides, and safely remove instance components. Preserve inherited or construction-script-owned components that cannot be structurally changed safely.
- Add retained, bounded editor generation and cleanup operations for one exact component. Do not mix generation or cleanup with unrelated actor mutations, and do not expose PIE control, runtime gameplay mutation, arbitrary PCG callbacks, or unrestricted generated-object editing.
- Require the current map snapshot, exact actor and component identities, graph snapshot where applicable, and a caller-generated operation ID. Reject stale state, active generation, PIE or simulation, unavailable World Partition cells, locked data layers, unsafe graph dependencies, and conflicting editor work.
- Track generated actors, components, partition actors, managed resources, dirty packages, and World Partition external packages without returning unbounded object lists. Integrate configuration and generated-output persistence with the existing transaction, rollback, `level_save`, partial-failure, replay, and reload-verification contracts.

### Verification

- Test native, instance-added, Blueprint-provided, partitioned, and non-partitioned PCG Components; graph assignment; supported settings and parameter overrides; generation; cleanup; removal; save; restart; and exact read-back.
- Test stale identities and snapshots, incompatible graphs and values, unsupported inherited structural edits, active-generation conflicts, limits, timeouts, replay, rollback, undo/redo, generation failure, cleanup failure, World Partition loading, external packages, and partial save failure.
- Run a native macOS and Windows acceptance that configures a bounded PCG Component, generates representative managed output, saves and reloads it, verifies exact state, cleans it up, and proves unrelated actors and components remain unchanged.

### Documentation and completion gate

- Document the PCG Component inspection and edit matrix, graph assignment, parameter overrides, generation and cleanup lifecycle, World Partition behavior, persistence, limits, exclusions, recovery, and a focused level example.
- Complete the feature only when exact PCG Components and their managed output can be configured, generated, cleaned up, saved, restarted, and verified without stale writes, duplicate replay effects, unbounded results, or unrelated level changes.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
