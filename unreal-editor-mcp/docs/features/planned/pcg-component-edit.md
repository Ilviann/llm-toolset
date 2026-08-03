---
feature_id: pcg-component-edit
status: planned
depends_on:
  - pcg-graph-authoring
  - level-edit
released_in: null
---

# `pcg-component-edit` — Level PCG node inspection and authoring

**Outcome:** Agents can inspect and author PCG-related nodes in levels, including actors, components, and other supported level objects.

**Depends on:**

- [`pcg-graph-authoring`](pcg-graph-authoring.md)
- [`level-edit`](../completed/level-edit.md)

**Planning note:** Review and update this detailed contract against the current executable tool catalog, companion foundation, level contracts, and supported Unreal public PCG APIs immediately before implementation. Only inspection and authoring of PCG-related nodes in levels—including actors, components, and other supported level objects—is stable functional scope for this feature. The tool mapping, operation shapes, identities, snapshots, generation and cleanup lifecycle, persistence, limits, and verification details below are provisional and are not implementation authority until that review is complete.

### Implementation

- Require Unreal's plugin manager to report the Engine plugin named `PCG` effectively enabled for the configured project, require the running editor to load it successfully, and require the matching `UnrealMCPPCG` graph inspection and mutation capabilities. Honor Engine defaults and explicit project overrides; never install or enable PCG, edit the project descriptor, or advertise component support from installed files or stale capability state alone.
- Extend bounded level inspection with PCG Component identity, assigned graph, parameter overrides, seed, activation, generation trigger, partitioning, generation state, managed-output summary, and supported runtime-generation settings.
- Extend level actor editing with typed operations to add supported instance PCG Components, assign or clear an exact PCG Graph, edit supported component settings and graph-parameter overrides, and safely remove instance components. Preserve inherited or construction-script-owned components that cannot be structurally changed safely.
- Add retained, bounded editor generation and cleanup operations for one exact component. Do not mix generation or cleanup with unrelated actor mutations, and do not expose PIE control, runtime gameplay mutation, arbitrary PCG callbacks, or unrestricted generated-object editing.
- Require the current map snapshot, exact actor and component identities, graph snapshot where applicable, and a caller-generated operation ID. Reject stale state, active generation, PIE or simulation, unavailable World Partition cells, locked data layers, unsafe graph dependencies, and conflicting editor work.
- Track generated actors, components, partition actors, managed resources, dirty packages, and World Partition external packages without returning unbounded object lists. Integrate configuration and generated-output persistence with the existing transaction, rollback, `level_save`, partial-failure, replay, and reload-verification contracts.

### Verification

- Test missing, disabled, unloaded, stale, or excluded project `PCG` configuration and prove every component inspection, edit, generation, and cleanup operation rejects before actor or asset mutation while the base level tools remain usable.
- Test native, instance-added, Blueprint-provided, partitioned, and non-partitioned PCG Components; graph assignment; supported settings and parameter overrides; generation; cleanup; removal; save; restart; and exact read-back.
- Test stale identities and snapshots, incompatible graphs and values, unsupported inherited structural edits, active-generation conflicts, limits, timeouts, replay, rollback, undo/redo, generation failure, cleanup failure, World Partition loading, external packages, and partial save failure.
- Run a native Windows acceptance that configures a bounded PCG Component, generates representative managed output, saves and reloads it, verifies exact state, cleans it up, and proves unrelated actors and components remain unchanged. Repeat on macOS when available as non-blocking follow-up.

### Documentation and completion gate

- Document the effective per-project `PCG` enablement prerequisite, Engine-default and project-override behavior, PCG Component inspection and edit matrix, graph assignment, parameter overrides, generation and cleanup lifecycle, World Partition behavior, persistence, limits, exclusions, recovery, and a focused level example. State that Unreal MCP never installs or enables PCG or edits the project descriptor.
- Complete the feature only when exact PCG Components and their managed output can be configured, generated, cleaned up, saved, restarted, and verified without stale writes, duplicate replay effects, unbounded results, or unrelated level changes, and every PCG operation remains unavailable unless Unreal reports `PCG` effectively enabled for the configured project.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
