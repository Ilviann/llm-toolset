---
feature_id: graph-scoped-inspect
status: completed
depends_on:
  - animation-blueprint-inspect
released_in: "0.39.0"
---

# `graph-scoped-inspect` — Graph summaries and explicit graph selection

**Outcome:** List Blueprint graph names, kinds, declared input/output parameters, and identity/ownership metadata; require one graph name or ID before returning graph details and nodes.

**Depends on:**

- `animation-blueprint-inspect`

## Implementation

- [x] Add bounded exact `graph_name` selection, mutually exclusive with `graph_id`; reject missing or ambiguous targets and unscoped node/pin/connection requests.
- [x] Include declared function/macro parameters in graph summaries. Return node counts and animation ownership/schema only for a selected graph; omitted sections with a selector return that graph's details, nodes, pins, and connections.
- [x] Keep complete asset graph fingerprints across output selections, bounded traversal, paging, inherited ownership, and read-only preservation.
- [x] Publish the capability and update Python schemas, native contracts, examples, and caller workflows without changing the companion API.

## Verification

- [x] Pass full Python and native suites, including graph names/IDs, parameters, inheritance, ambiguity, invalid inputs, pagination, snapshots, and non-mutation. The Python suite ran 148 tests with one unavailable-symlink skip; all 46 required native cases passed.
- [x] Pass Windows adaptive and supported forced-unity builds, full restart integration, focused Animation Blueprint restart integration, and binary packaging.
- [x] Pass documentation lint; retain macOS verification as preferred follow-up work.

[Inspector architecture](../../architecture/blueprint-inspector.md) · [Wire contracts](../../types/blueprint-inspector/contracts.md#queries-records-and-pages)
