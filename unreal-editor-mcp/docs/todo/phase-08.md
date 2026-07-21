# Phase 8 — Action-catalog infrastructure and core actions

**Outcome:** Agents can discover a small bounded set of function-call and variable actions valid for one graph without guessing node classes or mutating the Blueprint.

### Implementation

- Add read-only `blueprint_action_catalog` with exact text, owner class, function/member, node family, graph, and optional pin-context filters.
- Build results from Unreal's public Blueprint action/spawner and live schema filtering APIs. Do not expose unrestricted node-class construction or accept forged action signatures.
- Initially cover pure and impure function calls and variable get/set actions that pass the live graph, family, member, and optional pin-context filters.
- Return opaque action IDs bound to the bridge instance, target Blueprint class, graph schema and identity, structural snapshot, normalized query, and rebuildable action signature.
- Bound action-database scans, elapsed time, result count, encoded bytes, retained action records, cache lifetime, and concurrent catalog work. Publish the effective limits.
- Keep this phase mutation-free. Node creation moves to Phase 11.

### Verification

- Test pure and impure functions, static and instance context, inherited and local members, variable access, incompatible graph types, pin context, narrow filters, truncation, timeout, cache eviction, expiry, bridge restart, and stale snapshots.
- Confirm that forged names, class paths, signatures, expired IDs, cross-project IDs, and IDs from another graph or snapshot cannot invoke or resolve unsupported or internal actions.
- Prove catalog calls do not dirty, transact, compile, save, select, or otherwise mutate the Blueprint.

### Documentation and completion gate

- Document the inspect → narrow catalog workflow, action identity lifetime, filters, limits, invalidation rules, and initially supported action families.
- Complete the phase when representative Actor event, function, and macro graphs return small function and variable catalogs with stable bounded behavior.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
