---
feature_id: graph-scope-inspect
status: completed
depends_on:
  - inspect-limits
  - library-inspect
released_in: "0.59.0"
---
# Exact graph scope for inspection

**Depends on:**

- [`inspect-limits`](inspect-limits.md)
- [`library-inspect`](library-inspect.md)

## Contract

Return compact graph summaries by default. Internal graph content requires one graph_id or exact graph_name; a selector alone requests that graph contents. Public asset_inspect retains hierarchical selectors, adds graphs and exact graphs/<id-or-name>, rejects ambiguous names, and preserves whole-asset snapshots and existing atomic graph output.

## Verification

- [x] Implementation and focused regression fixtures.
- [x] Full Python/native suites, Windows build variants, packaging, and production-bridge restart validation.
- [x] Final release metadata and macOS follow-up tracking.

See [implementation evidence](../../plans/inspection-expansion.md).
