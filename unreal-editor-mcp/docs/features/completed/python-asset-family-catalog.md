---
feature_id: python-asset-family-catalog
status: completed
depends_on:
  - asset-family-foundation
released_in: "0.42.0"
---

# `python-asset-family-catalog` — Static Python asset-family catalog

**Outcome:** Approved built-in and companion families compose exact Python schemas, access gates, operation mappings, result handling, and native capability requirements from one static catalog.

**Depends on:**

- [`asset-family-foundation`](asset-family-foundation.md)

### Implementation

- Added immutable built-in publication entries for core, asset, level, Blueprint, Widget, gameplay-framework, game-data, and lifecycle operations. Each exact tool binding owns its schema, readonly or mutation access, native command requirement, bridge or local handler, and JSON or safe-YAML result policy.
- Moved shipped companion identities, schema revisions, exact operation branches, and integrated semantic-section mappings into the same catalog. Native modules and companions still cannot provide model-facing schemas dynamically.
- Added fail-closed construction checks for duplicate family IDs, public tools, companion identities, contribution keys, conflicting native command mappings, invalid access/handlers/result policies, and malformed requirements.
- Made server schema validation, access filtering, native dispatch, result rendering, capability-driven availability, and companion composition consume the same deterministic catalog. The public tool surface, names, order, exact schemas, MCP negotiation, and companion API v1 remain unchanged.

### Verification and completion evidence

- `tests/test_asset_family_catalog.py` covers deterministic composition, duplicate and conflicting entries, invalid descriptors, unavailable or malformed native command catalogs, readonly filtering, operation mapping, result policy, and schema isolation.
- Existing extension, server/stdio, schema, release-contract, UTF-8, and YAML tests cover exact companion intersection, unavailable native capabilities, MCP initialization/list/call behavior, argument rejection before dispatch, and unchanged safe-YAML output.
- The complete Python suite, production-socket lifecycle integration, UE 5.8 Windows verification, and base Win64 packaging are the 0.42.0 release gates. macOS native verification remains preferred follow-up work; Linux remains outside the supported and verified scope.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
