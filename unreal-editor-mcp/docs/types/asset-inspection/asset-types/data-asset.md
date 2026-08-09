# Data Asset

This family covers asset instances whose classes derive from `UDataAsset` or `UPrimaryDataAsset`, plus Blueprint and Data-Only Blueprint class variants. Data Assets have no universal domain schema, so inspection combines stable framework semantics with safe cumulative reflection across the exact class hierarchy.

## Instance root response

```yaml
asset:
  type: primary_data_asset
  class: /Script/MyGame.WeaponDefinition
  parent_type: /Script/Engine.PrimaryDataAsset
data_asset:
  value_source: asset_instance
  load_behavior: lazy_on_demand
primary_data_asset:
  primary_asset_id:
    type: WeaponDefinition
    name: DA_Axe
  asset_bundles:
    count: 2
    selector: asset_bundles
properties:
  count: 4
  items:
    - name: DisplayName
      declared_by: /Script/MyGame.WeaponDefinition
      type: text
      value: Wood Axe
    - name: BaseDamage
      declared_by: /Script/MyGame.WeaponDefinition
      type: float
      value: 35.0
    - name: Mesh
      declared_by: /Script/MyGame.WeaponDefinition
      type: soft_object</Script/Engine.StaticMesh>
      value: /Game/Weapons/SM_Axe.SM_Axe
    - name: AllowedActorClasses
      declared_by: /Script/MyGame.WeaponDefinition
      type: array<class</Script/Engine.Actor>>
      value:
        kind: array
        item_type: class</Script/Engine.Actor>
        count: 18
        selector: properties/AllowedActorClasses
  page:
    size: 10
    index: 0
    count: 1
    returned: 4
    total_items: 4
    has_previous: false
    has_next: false
    snapshot_id: 40_hex_snapshot
selectors: [properties, asset_bundles]
```

## Type and inheritance rules

- A non-primary instance uses `type: data_asset`. A `UPrimaryDataAsset` descendant uses `type: primary_data_asset` because Primary Asset identity, Asset Manager loading, and bundles add meaningful framework semantics.
- `asset.class` is the exact class of the stored asset object. `parent_type` is its exact immediate native or Blueprint-generated superclass because DataAsset inheritance defines the property schema and is semantically meaningful.
- `value_source: asset_instance` distinguishes authored instance values from class defaults on a Blueprint class asset.
- `load_behavior` reports the effective DataAsset load behavior.
- Inspection cumulatively includes every important safe inherited and derived property, with `declared_by` provenance and without duplicating a property in multiple class layers.

## Primary Data Asset block

`primary_asset_id` reports the exact effective Primary Asset type and name returned by the asset. If an override produces no valid ID, report validity explicitly rather than fabricating one from the package name.

`asset_bundles` returns a bounded page of bundle records. Each bundle exposes its name and a collection descriptor for its referenced asset paths; a large bundle's assets use an exact nested selector and ordinary zero-based paging. Asset bundle references are not recursively inspected.

## Property index and exact selectors

`properties` returns a zero-based page of all important supported property records in deterministic declaring-class/property order. The root includes its first page at the default size of 10. `properties/<property-name>` returns one exact scalar or struct property, or the first page of an exact collection property.

Scalar, enum, name, string, text, exact object/class references, soft references, and bounded structs use direct semantic YAML values. A null reference is explicit. Struct fields retain their names and types where needed for unambiguous analysis.

Every array, set, or map value uses the shared collection descriptor and selector rules, including collections nested inside a struct or another collection item. The selected property header repeats its exact name, canonical type, declaring type, and full collection count.

### Array example

```yaml
asset:
  type: primary_data_asset
selection:
  selector: properties/AllowedActorClasses
property:
  name: AllowedActorClasses
  declared_by: /Script/MyGame.WeaponDefinition
  type: array<class</Script/Engine.Actor>>
collection:
  kind: array
  count: 18
items:
  - {index: 0, value: /Game/Weapons/BP_AxeActor.BP_AxeActor_C}
  - {index: 1, value: /Game/Weapons/BP_HeavyAxeActor.BP_HeavyAxeActor_C}
page:
  size: 2
  index: 0
  count: 9
  returned: 2
  total_items: 18
  has_next: true
  snapshot_id: 40_hex_snapshot
```

### Map example

```yaml
selection:
  selector: properties/DamageBySurface
property:
  name: DamageBySurface
  type: map<enum</Script/PhysicsCore.EPhysicalSurface>, float>
collection:
  kind: map
  count: 3
entries:
  - {index: 0, key: SurfaceType_Default, value: 35.0}
  - {index: 1, key: SurfaceType1, value: 50.0}
page:
  size: 2
  index: 0
  count: 2
  returned: 2
  total_items: 3
  has_next: true
  snapshot_id: 40_hex_snapshot
```

Arrays preserve authored order. Sets and maps use deterministic type-aware canonical ordering so page boundaries remain stable for one snapshot. Their zero-based indexes are not durable keys.

## Blueprint class variants

Proposed class-asset types are:

- `data_asset_blueprint` for a Blueprint-generated `UDataAsset` descendant;
- `primary_data_asset_blueprint` for a Blueprint-generated `UPrimaryDataAsset` descendant.

These responses use `value_source: class_defaults`, include the exact generated and immediate parent class identities, and expose Blueprint variables, functions, macros, event graphs, and selectors when present. Data-Only Blueprints normally have no logic graphs but still expose inherited and overridden class-default properties. Collection defaults use the same property selectors and paging rules as instance assets; selected logic graphs follow the complete/opted-in-partial graph rules.

An instance whose class happens to be Blueprint-generated remains `data_asset` or `primary_data_asset` with `value_source: asset_instance`; it is not mislabeled as the Blueprint class asset that defines its schema.

## Exclusions and limitations

- Do not expose editor thumbnails, import metadata, package serialization, property memory offsets, raw export text, delegates, transient fields, or editor-only state.
- Do not recursively inspect referenced assets. Return their exact semantic paths.
- Do not traverse arbitrary instanced UObject graphs in `asset-inspect-data`. Report an explicit property limitation with exact declared type; a future bounded subobject selector can extend this safely.
- Never return bulk or media payloads stored or referenced by a property.
- Skip deprecated properties by default unless they materially affect effective behavior and no supported replacement represents the same setting.

## Implementation implications

- Data Assets are not currently a published inspection family and cannot reuse the Data Table row contract directly.
- Implement exact DataAsset/PrimaryDataAsset classification, safe instance-property traversal, Primary Asset ID and bundle extraction, cumulative declaring-type provenance, shared property encoding, deterministic collection ordering, zero-based indexed pages, snapshotting, and exact nested selector routing.
- Blueprint class variants can reuse common Blueprint graph/member collectors but require a new non-Actor Blueprint-family classifier and DataAsset class-default adapter.
- No domain companion is required for the base framework. Companions may later replace generic safe properties with richer namespaced semantics for exact domain subclasses while preserving the same collection paging contract.

## Open questions

- None at the current requirements layer. DataAsset and PrimaryDataAsset Blueprint class assets, including Data-Only Blueprint variants, are included in `asset-inspect-data` alongside instance assets.
