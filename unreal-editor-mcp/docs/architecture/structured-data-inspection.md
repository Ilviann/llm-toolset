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

## Expanded read codecs

`UnrealMCPGameplayAttributeInspection.h` is a private reflection-only reader shared by the Game Data and K2 codecs. Exact `/Script/GameplayAbilities.GameplayAttribute` identity selects it; it parses bounded exported identity fields and resolves only already-loaded `UStruct`/`FProperty` metadata. Compatibility mirrors floating-point or GameplayAttributeData-backed properties and verifies retained name/owner metadata. UE 5.7 three-field exports and unresolved values use the same record. No GAS header, module dependency, reference load, or mutation decoder is introduced.

The property codec retains its editable allowlist; its read-only fallback uses the Game Data encoder for safe reflected structures and collections. Unsupported row fields become individual unavailable values, while depth and collection-limit failures remain hard errors. Public class-default/component selectors reuse this structured view, including declaring-property and archetype provenance.
