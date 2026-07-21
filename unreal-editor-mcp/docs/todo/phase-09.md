# Phase 9 — Expanded action-catalog families

**Outcome:** Agents can discover the remaining supported event, flow-control, cast, literal, and operator actions through the same bounded catalog contract.

### Implementation

- Extend `blueprint_action_catalog` with event, flow-control, cast, literal, and common operator families that pass the live graph, Blueprint family, and optional pin-context filters.
- Preserve the Phase 8 action-ID, rebuildable-signature, cache, expiry, snapshot, and scan-limit contracts without adding family-specific bypasses.
- Apply live schema filtering for unique events, latent calls, static and instance contexts, inherited members, local members, and incompatible graph types.
- Keep the catalog mutation-free and keep node construction unavailable until Phase 10.

### Verification

- Test unique events, latent calls, flow-control nodes, casts, literals, common operators, family restrictions, pin contexts, wildcard candidates, narrow filters, and bounded truncation.
- Repeat forgery, cross-project, cross-graph, expired-ID, stale-snapshot, cache-eviction, timeout, and mutation-free proofs for every added family.
- Prove representative queries remain small enough for the default model context.

### Documentation and completion gate

- Document every supported action family, its filters and restrictions, and focused catalog recipes.
- Complete the phase when representative Actor event, function, and macro graphs return small context-valid catalogs across all supported action families.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
