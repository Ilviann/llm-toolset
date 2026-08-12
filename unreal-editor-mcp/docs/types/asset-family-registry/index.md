# Built-in asset-family registry contracts

## Descriptor and selection records

- `FUnrealMCPAssetFamilyDescriptor` identifies one built-in family by stable ID, resolved native class, exact/derived policy, signed priority, required modules, common bounds, named limits, capability declarations, and optional typed adapters.
- `FUnrealMCPAssetFamilyCapabilities` declares inspection, creation, and editing independently. Declaration and adapter presence must agree.
- `FUnrealMCPAssetFamilySelection` returns the exact frozen descriptor and bounded missing-module evidence. Classification is evaluated before dependency and capability admission so failures remain diagnostic without retargeting.
- `FUnrealMCPAssetFamilyLimits` bounds semantic records, recursively measured value nodes/depth/bytes, selector routes and depth, and snapshot contributions and bytes. `FUnrealMCPAssetFamilyLimit` carries family-specific positive named limits.

## Adapter contexts

- `FUnrealMCPAssetFamilyInspectionContext` carries the already resolved asset, canonical identity/snapshot precondition, decoded selector, bounded paging controls, and graph presentation choices.
- `FUnrealMCPAssetFamilyCreationContext` carries an admitted outer, resolved class, asset name, and canonical object path. The later authoring kernel owns transactions, persistence, cleanup, and rollback.
- `FUnrealMCPAssetFamilyEditContext` carries the resolved asset, identity/snapshot precondition, one typed operation identity, and bounded semantic value records. It does not authorize persistence or access by itself.
- `IUnrealMCPAssetFamilyInspectionAdapter`, `IUnrealMCPAssetFamilyCreationAdapter`, and `IUnrealMCPAssetFamilyEditingAdapter` are independent trusted base-native interfaces. An adapter may implement any combination through its descriptor.

## Semantic builders and values

- `FUnrealMCPAssetFamilyValueRecord` is one bounded path/type/value contribution. Values remain native JSON-neutral `FUnrealMCPValue` records; the builder measures the complete typed tree rather than trusting an adapter-supplied cost, and adapters do not encode transport JSON or YAML.
- `FUnrealMCPAssetFamilyDocumentBuilder` rejects invalid records, duplicate paths, count overflow, and byte overflow.
- `FUnrealMCPAssetFamilySelectorRouter` registers bounded collision-free prefixes, freezes into stable order, and resolves the longest matching prefix. A route is pageable, graph-like, or neither; it cannot be both pageable and graph-like.
- `FUnrealMCPAssetFamilySnapshotBuilder` accepts unique bounded identity/value contributions and hashes them in identity order, independent of contribution order.

## Registry lifecycle

`FUnrealMCPAssetFamilyRegistry::Register` accepts trusted compiled descriptors only before freeze. `Freeze` validates again, sorts deterministically, captures required-module availability, and computes one restart-stable fingerprint. `Select` requires the frozen state, resolves exactly one highest-priority family, then admits the requested independent capability. The base module freezes this registry before constructing the bridge, and the command catalog rejects a mutable registry.
