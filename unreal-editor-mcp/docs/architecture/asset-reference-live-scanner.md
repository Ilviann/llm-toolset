# Asset reference live scanner

## Ownership

`FUnrealMCPAssetReferenceLiveScanner` owns open-editor evidence and bounded direct strong-reference discovery across already-loaded UObjects. Its source is `Private/UnrealMCPAssetReferenceLiveScanner.{h,cpp}`.

## Dependency direction

The snapshot builder passes only the resolver's already-loaded target pointer and shared record collection. The scanner uses `UAssetEditorSubsystem`, `FThreadSafeObjectIterator`, and `FReferenceFinder`; it has no Asset Registry query, cursor, deletion, Blueprint, or level dependency.

## Invariants

- An unloaded target produces an explicit unsupported live scan and is never loaded.
- Direct reference discovery uses reflected collection and `AddReferencedObjects`, not arbitrary `Serialize` overrides.
- Loaded-object, property-name, and total-record limits are enforced.
- Weak pointers, external code references, and recursively reachable object graphs remain excluded.

## Verification

Run the asset-reference Automation Test, the full UnrealMCP Automation suite, and normal plus forced-unity Editor builds.
