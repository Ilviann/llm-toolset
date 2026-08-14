# Data Asset and Data Table inspection

## Ownership

`UnrealMCPDataInspectionAdapters` in `UnrealMCPContent` owns the `data_asset` and `data_table` built-in family descriptors and their root/selector records. The Data Asset adapter owns instance classification, Primary Asset identity, Asset Bundles, and reflected-property orchestration. The Data Table adapter owns table settings, schema/row/column views, explicit page indexing, and reuse of the game-data inspection snapshot. Data Asset Blueprint class classification remains in the core Blueprint adapter so its established graph/member collectors stay authoritative.

## Dependency direction

The Content domain registers both exact-and-derived families before registry freeze. Adapters depend on Asset Registry metadata, `FUnrealMCPGameDataInspectionBuilder`, and the Blueprint-owned structured-data helper. The neutral coordinator remains family-independent; Python keeps the one unchanged `asset_inspect` schema and deterministic safe-YAML result policy.

## Invariants

- `UDataAsset` and `UPrimaryDataAsset` instances select the higher-priority `data_asset` family; `UDataTable` and derived table classes select `data_table`; unrelated assets retain neutral or Blueprint dispatch.
- Data Asset roots expose instance/class identity, meaningful inheritance, lazy load behavior, Primary Asset validity, bounded bundles, and the first reflected-property page. Blueprint class variants use class defaults and retain Blueprint graphs, functions, macros, and variables.
- Data Table roots expose exact row-struct identity/kind, import/client-build policy, complete bounded schema, and a sorted row-name page. `rows`, exact row selectors, nested row-field selectors, and `columns/<field>` all share one query-independent game-data snapshot.
- CSV/JSON source, import filenames, arbitrary serialization, recursive object graphs, media/bulk payloads, and referenced-asset traversal are never returned.
- Inspection preserves package dirtiness and Blueprint status. `game_data_inspect` remains separately published and wire-compatible.

## Verification

Focused native Automation exercises both descriptors and their primary selector paths. The core asset-inspection test covers Data Asset Blueprint classification. Full Python, native, production-socket, build-mode, and Win64 packaging gates protect the common facade and release metadata.
