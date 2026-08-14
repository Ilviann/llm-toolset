# Asset-family registry contracts

## Descriptor and selection records

- `FUnrealMCPAssetFamilyDescriptor` identifies one built-in or admitted companion family by stable ID, resolved native class, exact/derived policy, signed priority, required modules, common bounds, named limits, capability declarations, declared selector routes, optional typed adapters, and an inspection-overlay flag.
- `FUnrealMCPAssetFamilyCapabilities` declares inspection, creation, and editing independently. Declaration and adapter presence must agree.
- `FUnrealMCPAssetFamilySelection` returns the exact frozen descriptor and bounded missing-module evidence. Classification is evaluated before dependency and capability admission so failures remain diagnostic without retargeting.
- `FUnrealMCPAssetFamilyLimits` bounds semantic records, recursively measured value nodes/depth/bytes, selector routes and depth, and snapshot contributions and bytes. `FUnrealMCPAssetFamilyLimit` carries family-specific positive named limits.

## Adapter contexts

- `FUnrealMCPAssetFamilyInspectionContext` carries the already resolved asset, canonical identity/snapshot precondition, decoded selector, bounded paging controls, graph presentation choices, and explicit-presence flags for paging and partial-graph request fields. Those flags preserve validation distinctions between omitted defaults and caller-supplied values.
- `FUnrealMCPAssetFamilyCreationContext` carries an admitted outer, resolved class, asset name, and canonical object path. The implemented [asset-authoring kernel](../asset-authoring-kernel/index.md) owns transactions, persistence, cleanup, and rollback.
- `FUnrealMCPAssetFamilyEditContext` carries the resolved asset, identity/snapshot precondition, one typed operation identity, and bounded semantic value records. It does not authorize persistence or access by itself.
- `IUnrealMCPAssetFamilyInspectionAdapter`, `IUnrealMCPAssetFamilyCreationAdapter`, and `IUnrealMCPAssetFamilyEditingAdapter` are independent trusted base-native interfaces. An adapter may implement any combination through its descriptor.

## Semantic builders and values

- `FUnrealMCPAssetFamilyValueRecord` is one bounded path/type/value contribution. Values remain native JSON-neutral `FUnrealMCPValue` records; the builder measures the complete typed tree rather than trusting an adapter-supplied cost, and adapters do not encode transport JSON or YAML.
- `FUnrealMCPAssetFamilyDocumentBuilder` rejects invalid records, duplicate paths, count overflow, and byte overflow.
- `FUnrealMCPAssetFamilySelectorRouter` registers bounded collision-free prefixes, freezes into stable order, and resolves the longest matching prefix. A route is pageable, graph-like, or neither; it cannot be both pageable and graph-like.
- `FUnrealMCPAssetFamilySnapshotBuilder` accepts unique bounded identity/value contributions and hashes them in identity order, independent of contribution order.

## Registry lifecycle

`FUnrealMCPAssetFamilyRegistry::Register` accepts trusted compiled descriptors only before freeze. `Freeze` validates again, sorts descriptors and routes deterministically, captures required-module availability, and computes one restart-stable fingerprint. `SelectPrimary` resolves exactly one highest-priority non-overlay family. `SelectInspectionOverlays` returns every matching enabled inspection overlay in family-ID order; overlays do not compete with the primary classification. The base module admits companions before freezing this registry and constructing the bridge, and the command catalog rejects a mutable registry.
