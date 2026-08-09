# Data Table

This family covers `UDataTable` assets backed by a native or user-defined row struct. The root response is a compact typed index; selectors expose bounded rows and optional column-oriented projections without duplicating the existing `game_data_inspect` implementation.

## Root response

```yaml
asset:
  type: data_table
data_table:
  load_behavior: lazy_on_demand
  row_struct:
    path: /Game/Data/ST_WeaponStats.ST_WeaponStats
    kind: user_defined
  row_count: 240
  client_build: included
  import:
    key_field: null
    ignore_extra_fields: false
    ignore_missing_fields: false
    preserve_existing_values: false
schema:
  field_count: 4
  fields:
    - {name: Damage, type: float, declared_by: /Game/Data/ST_WeaponStats.ST_WeaponStats}
    - {name: Range, type: float, declared_by: /Game/Data/ST_WeaponStats.ST_WeaponStats}
    - {name: WeaponClass, type: class</Script/Engine.Actor>, declared_by: /Game/Data/ST_WeaponStats.ST_WeaponStats}
    - {name: Tags, type: array<name>, declared_by: /Game/Data/ST_WeaponStats.ST_WeaponStats}
rows:
  count: 240
  index:
    - {name: Axe, selector: rows/Axe}
    - {name: Bow, selector: rows/Bow}
    - {name: Crossbow, selector: rows/Crossbow}
    - {name: Dagger, selector: rows/Dagger}
    - {name: Hammer, selector: rows/Hammer}
    - {name: Longsword, selector: rows/Longsword}
    - {name: Mace, selector: rows/Mace}
    - {name: Spear, selector: rows/Spear}
    - {name: Staff, selector: rows/Staff}
    - {name: Wand, selector: rows/Wand}
  page:
    size: 10
    index: 0
    count: 24
    returned: 10
    total_items: 240
    has_previous: false
    has_next: true
    snapshot_id: 40_hex_snapshot
selectors: [schema, rows, columns]
```

The root row-name index is bounded and pageable. It defaults to ten names and never attempts to place every row value into the default response.

## Root property rules

- A standard `UDataTable` uses `type: data_table` and omits `parent_type` because UObject inheritance adds no useful semantic distinction. A custom DataTable subclass additionally reports its exact class and meaningful immediate `parent_type`, then cumulatively adds important subclass properties.
- `row_struct.path` is the exact live struct asset or native path. `kind` is `native` or `user_defined`.
- `load_behavior` records the framework's lazy-on-demand asset behavior.
- `client_build` is `stripped` when `bStripFromClientBuilds` is true and `included` otherwise. This materially affects whether clients can access the table after cooking.
- `import` reports the effective import key and missing/extra-field preservation policies. These are asset properties even though `asset_inspect` does not import CSV or JSON.
- The root schema lists every bounded reflected field with authored name, canonical semantic type, and declaring struct. It includes important inherited fields and derived row-struct fields, each exactly once.
- The root row index contains sorted row names and exact row selectors. `page_size` and `page_index` select a deterministic slice of the sorted names.

## Schema selector

`schema` returns the complete bounded field schema. In addition to the compact root fields, it may include internal property name when different, container element/key/value types, enum identity and allowed names, struct identity, compatible object/class constraint, effective row-struct default, tooltip, and relevant safe property flags.

Do not emit native memory offsets, property GUIDs, C++ layout, raw struct serialization, editor expansion state, or source-file locations. User-defined member GUIDs may be retained internally for snapshot stability but are not normal semantic output.

## Row selectors

- `rows` returns a bounded page of complete rows sorted by row name. `page_size` defaults to 10 and `page_index` selects the requested page.
- `rows/<row-name>` returns one exact row.

Every indexed page returns `size`, `index`, total page `count`, records `returned`, `total_items`, `has_previous`, `has_next`, and `snapshot_id`. The same query-independent snapshot is expected across pages; if the asset changes, the caller restarts paging rather than combining mismatched snapshots. Exact single-row selection is not paged.

Example exact row:

```yaml
asset:
  type: data_table
selection:
  selector: rows/Axe
row:
  name: Axe
  values:
    Damage: 35.0
    Range: 175.0
    WeaponClass: /Game/Weapons/BP_Axe.BP_Axe_C
    Tags:
      kind: array
      item_type: name
      count: 2
      selector: rows/Axe/Tags
```

Values use the existing bounded reflected row-value codec: finite scalars, enum names, exact object/class references, arrays, sets, maps, and nested structs. YAML presentation removes the JSON codec's transport wrappers where doing so remains unambiguous; canonical typed records retain exact type discriminators internally.

Array, set, or map row fields are collection descriptors rather than inline values. Their exact row-field selectors use ordinary zero-based paging, including for collections nested inside row structs or collection elements. Column projections over collection-valued fields likewise return per-row collection descriptors and exact child selectors instead of expanding every element.

## Column selectors

`columns/<field-name>` returns a bounded page of `{row, value}` records for one exact schema field. This is a new semantic projection over the same row snapshot and codec, intended for comparing balance or configuration values without requesting every other field in each row.

```yaml
asset:
  type: data_table
selection:
  selector: columns/Damage
column:
  name: Damage
  type: float
values:
  - {row: Axe, value: 35.0}
  - {row: Bow, value: 22.0}
page:
  size: 2
  index: 0
  count: 120
  returned: 2
  total_items: 240
  has_next: true
  snapshot_id: 40_hex_snapshot
```

The root schema pinpoints each valid field name, and `selectors` advertises `columns`. Column pages use the same `page_size`, `page_index`, row-count, collection, value-depth, and snapshot rules as row inspection.

## Exclusions

- Do not return CSV/JSON import source, filenames, asset-import metadata, raw row memory, unrestricted Unreal export text, delegates, editor selection state, or filesystem paths.
- Do not recursively inspect referenced assets. Preserve exact semantic reference paths only.
- Curve Tables are a separate asset family and are not silently treated as Data Tables.
- If one row value cannot be represented by the safe codec, return an explicit bounded limitation or stable error according to the final partial-result policy; never fall back to raw serialization text.

## Framework inheritance

- A custom `UDataTable` subclass retains all important DataTable settings, schema, and rows and adds important safe properties introduced by each supported subclass layer.
- A derived row struct's schema cumulatively includes important inherited struct fields and its newly declared fields with exact declaring-struct provenance.
- Unknown asset subclasses may add bounded safe reflected class-default properties. Skip transient, editor-only, delegate, recursive object, bulk, and media fields.

## Implementation implications

- The existing game-data service already resolves Data Tables, row structs, schemas, sorted rows, typed nested values, snapshots, bounds, exact row filters, and cursor pages. `asset_inspect` should reuse its collectors and snapshots but expose explicit page size/index slicing instead of its cursor protocol.
- Root row-name indexing and exact `rows/<name>` routing can reuse the existing sorted names and row filter. The accepted `columns/<field>` projection is a small new family-specific view over the same validated row data.
- The current 64-field, 64-container-item, depth-four, 2,048-row scan, and snapshot rules remain authoritative unless this feature explicitly revises them later.

## Open questions

- None at the current requirements layer. `page_index` and every other numeric index are zero-based, and `columns/<field-name>` is included in `asset-inspect-data`.
