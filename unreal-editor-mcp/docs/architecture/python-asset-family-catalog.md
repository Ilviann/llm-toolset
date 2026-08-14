# Python asset-family catalog

## Ownership

`unreal_editor_mcp/asset_family_catalog.py` owns the immutable Python publication descriptors and deterministic configuration-specific composition. Schema definitions remain narrowly grouped under `tool_catalog_families/`; the catalog binds those exact definitions to readonly or mutation access, required native commands, bridge or local handlers, and JSON or safe-YAML result policies. The same entries own shipped companion identities, schema revisions, exact contribution branches, and integrated inspection-section mappings. `tool_catalog.py` and `extension_catalog.py` retain compatibility exports without owning duplicate policy.

## Dependency direction

Tool-family schema modules do not depend on the catalog or server. The static catalog imports those definitions, validates every built-in and companion entry once at import, and returns deep-copied public schemas plus immutable dispatch metadata. `server.py` consumes one composition for listing, schema validation, access admission, command dispatch, and result rendering. Native capability data can narrow a composition but cannot add a schema, handler, command mapping, or family unknown to the shipped Python release.

## Invariants

- Family IDs, public tool names, companion identities, contribution keys, and native command mappings are unique. Access classes, handlers, result policies, schema revisions, and native requirements are exact and validated before serving.
- Readonly and lifecycle filtering preserves catalog order. When native `capabilities.commands` is present, a tool is published only when all of its required commands are listed; missing or malformed command lists fail closed without changing unrelated schemas. An absent command field retains offline startup publication so the server can report editor unavailability through normal calls.
- Built-in schemas and all approved companion branches are Python-owned constants. Exact native companion API/schema identity, readiness, expected asset-family IDs, inspection-only operation shape, startup access, and known Python entries must intersect before a companion becomes effectively ready.
- Server dispatch uses the catalog's operation mapping. Only catalog-declared local handlers bypass the bridge, and only catalog-declared result policies transform success output. `asset_inspect` remains the sole safe-YAML result; every other released tool retains JSON text.
- Composition deep-copies schemas. Capability transitions and companion additions cannot mutate the static catalog or leak branches into a later composition.

## Verification

Run `python -m unittest tests.test_asset_family_catalog tests.test_extension_catalog tests.test_server_stdio tests.test_contracts -v`, then the complete Python suite. Production-socket integration verifies exact matching native commands and unchanged MCP initialization, listing, calls, authentication, and result handling.

[Types and contracts](../types/python-asset-family-catalog/index.md) · [Architecture index](index.md)
