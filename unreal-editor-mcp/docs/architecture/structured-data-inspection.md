# Structured-data inspection

## Ownership

`UnrealMCPStructuredDataInspection` in `UnrealMCPBlueprint` owns the shared read-only reflected-property view used by Data Assets, Data Asset Blueprint class defaults, and Data Table row values. It enumerates bounded cumulative properties, preserves declaring-type provenance, emits safe scalar/struct values, replaces collections with exact selector descriptors, and resolves zero-based nested collection pages. `UnrealMCPGameDataValueCodec` remains the scalar/reference/type authority.

## Dependency direction

The helper depends on typed wire records and the existing Blueprint-owned game-data value codec. Data-family adapters provide a live object or struct/data pair, selector prefix, page request, and stable snapshot. The helper does not resolve assets, classify families, own MCP schemas, mutate data, load referenced objects, or encode final YAML.

## Invariants

- Only authored Data Asset properties and live Data Table row fields enter the semantic view; transient, deprecated, and editor-only properties are omitted.
- Safe scalar, enum, string, text, object/class path, soft-reference, and bounded struct values reuse the established codec. Instanced objects, delegates, interfaces, and unsafe graphs produce explicit typed limitations rather than raw export text.
- Arrays preserve authored order. Sets and maps use deterministic canonical value/key order; their indexes remain snapshot-local presentation indexes.
- Every collection, including one nested in a struct or collection element, returns an exact uppercase UTF-8 selector. Nested paths use `items/<index>` or `entries/<index>/(key|value)` and never recurse into referenced assets.
- Property count, row-field count, value depth, collection scans, response records, bytes, and selector depth remain bounded. Snapshot material is query-independent.

## Verification

`UnrealMCP.AssetInspect.DataAssetsTablesSelectorsAndSnapshots` covers property indexes, array pages, row values, nested selectors, snapshots, unsupported instanced values, and unchanged package dirtiness. Python contracts verify the shared helper continues to call `UnrealMCPGameDataValueCodec` and remains behind family adapters.
