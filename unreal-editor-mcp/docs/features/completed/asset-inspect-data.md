---
feature_id: asset-inspect-data
status: completed
depends_on:
  - asset-inspect-core
  - native-domain-modules
released_in: "0.46.0"
---

# `asset-inspect-data` — Data Asset and Data Table inspection

**Outcome:** The established `asset_inspect` tool analyzes structured project data through the same deterministic YAML, selector, paging, snapshot, and error contracts as gameplay Blueprints.

**Implementation status:** Completed in 0.46.0. Windows passed the Python, native Automation, production-socket, adaptive/forced-unity/non-unity build, and base-plugin packaging gates. macOS verification remains preferred follow-up work; Linux is out of scope.

**Depends on:**

- [`asset-inspect-core`](asset-inspect-core.md)
- [`native-domain-modules`](native-domain-modules.md)

### Implementation

- Added exact deep families for Data Asset and Primary Data Asset instances plus Data Tables; Blueprint and Data-Only Blueprint Data Asset classes reuse the existing Blueprint graph/member collectors with class-default structured properties.
- Added cumulative reflected-property records with declaring-type provenance, explicit limitations for unsupported values, exact UTF-8 selectors, zero-based pages, deterministic set/map order, and nested collection routing without recursive object traversal.
- Added Primary Asset identity and bounded Asset Bundle selectors. References remain exact paths and are never recursively inspected.
- Reused the game-data schema, row-value codec, sorted row set, scan limits, and query-independent snapshot for Data Table roots, row pages, exact rows, nested collections, and `columns/<field>` projections. `game_data_inspect` remains compatible and published.
- Registered `data_asset` and `data_table` behind the frozen family registry and published the native `asset_inspect_data` capability without changing the model-facing tool schema or companion API v2.

### Verification evidence

- `UnrealMCP.AssetInspect.DataAssetsTablesSelectorsAndSnapshots` covers Primary Data Asset properties, unsupported instanced objects, Data Table schema/rows, nested arrays, column projections, snapshots, invalid graph flags, and read-only package preservation.
- The existing core asset-inspection Automation suite covers Data Asset Blueprint classification alongside every prior Blueprint, selector, paging, limit, and neutral-family regression.
- Python contract and full-suite tests cover registration, codec reuse, exact unchanged tool publication, release metadata, and deterministic safe-YAML framing.
- Windows release verification covers adaptive, true forced-unity, explicit non-unity, headless production-socket lifecycle, and Win64 packaging from Unreal Engine 5.8.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
