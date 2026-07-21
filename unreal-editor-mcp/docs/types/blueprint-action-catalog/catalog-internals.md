# Action catalog internal boundaries

`FActionCatalogQuery` owns exact JSON-shape validation and normalized asset, graph, snapshot, text, owner, function, member, family, and limit filters. The facade verifies the authoritative inspection snapshot, resolves the live Blueprint, K2 graph, and optional pin, and binds those identities into the query digest.

`ScanActions` owns target-class-first then global bounded traversal of `FBlueprintActionDatabase`, live `FBlueprintActionFilter` checks, supported-family classification, deduplication, deterministic sorting, and bounded candidate record encoding. The facade owns expiry, eviction, opaque action IDs, rebuild signatures, bridge/snapshot/schema identity, and cached public result reconstruction. Adding a Phase 10 action family changes the focused classifier/encoder without changing cache identity ownership.
