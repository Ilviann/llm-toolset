---
feature_id: pcg-graph-authoring
status: planned
depends_on: []
released_in: null
---

# `pcg-graph-authoring` — Procedural Content Generation graph authoring

**Outcome:** Agents can discover, inspect, create, edit, save, and read back bounded Unreal PCG Graph assets without exposing unrestricted code execution or altering unrelated graph content.

### Implementation

- Add a compact PCG Graph inspection and mutation surface that is available only when Unreal's Procedural Content Generation plugin and the matching native capability are ready.
- Discover exact mounted PCG Graph assets and inspect graph settings, parameters, nodes, typed node settings, pins, edges, subgraph references, comments, positions, and a deterministic structural snapshot.
- Create exact project-owned PCG Graph assets and support bounded transactional operations for nodes, settings, parameters, connections, subgraph references, comments, and positions. Require stable node and pin identities plus the latest graph snapshot for existing-content mutations.
- Resolve node types and pin compatibility through Unreal's live PCG APIs. Use a capability-advertised allowlist of supported settings and typed values; reject unknown settings, invalid connections, recursive subgraphs, unsafe asset references, custom HLSL, arbitrary Blueprint execution, and supplied code.
- Bound graph discovery, node and edge counts, nested values, asset-reference resolution, transaction work, diagnostics, execution time, and response size. Preserve unrelated nodes, settings, parameters, graph metadata, and prior dirty state on success or rejection.
- Reuse the authenticated bridge, operation ledger, stable errors, mount policy, editor transactions, explicit saving, postcondition read-back, replay handling, and lost-response reconciliation.

### Verification

- Test discovery, creation, each supported node/settings family, parameters, connections, disconnections, subgraphs, comments, movement, removal, saving, reload, and exact structural read-back.
- Test incompatible pins, subgraph cycles, unsupported or unsafe settings, malformed typed values, invalid asset paths, limits, stale identities and snapshots, transaction rollback, undo/redo, replay, timeouts, and unchanged-content fingerprints.
- Verify the base plugin remains usable when PCG support is absent or disabled, and run the complete base and PCG authoring suites natively on macOS and Windows.

### Documentation and completion gate

- Document PCG capability detection, supported graph/node/settings operations, typed values, identity and snapshot rules, limits, exclusions, saving, recovery, and a representative graph-authoring example.
- Complete the feature only when a representative PCG Graph can be created, edited, saved, restarted, and read back exactly while unsupported operations fail closed and unrelated graph content remains unchanged.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
