# Asset reference target resolver

## Ownership

`FUnrealMCPAssetReferenceTargetResolver` owns exact mounted object-path syntax, Asset Registry lookup, exact-path and mount verification, transient-object rejection, non-loading lookup of an already-loaded target, and bounded target metadata. Its source is `Private/UnrealMCPAssetReferenceTargetResolver.{h,cpp}`.

## Dependency direction

The snapshot builder calls the resolver before either scanner. Registry and live scanners reuse only its mount-name formatter. The resolver depends on public Asset Registry, package-name, and loaded-object lookup APIs and never depends on pagination or deletion.

## Invariants

- Resolution accepts one exact object path and never accepts a package, filesystem path, subobject, traversal segment, transient object, or unresolved asset.
- Registry lookup does not load the target or any referencer package.
- Returned metadata is bounded and identifies the exact registry asset and whether that object was already loaded.

## Verification

Run `tests.test_contracts`, `UnrealMCP.AssetReferences.RegistryLiveMemoryAndCursors`, and the normal and forced-unity Editor builds.
