# Asset reference registry scanner

## Ownership

`FUnrealMCPAssetReferenceRegistryScanner` owns the serialized-package, management, and searchable-name inbound scans, referencer-identifier expansion, per-category counts/status, and registry record encoding. Its source is `Private/UnrealMCPAssetReferenceRegistryScanner.{h,cpp}`.

## Dependency direction

The snapshot builder supplies one resolved target and a registry serial boundary. The scanner uses public Asset Registry and Asset Manager queries plus the target resolver's mount formatter. It never loads candidate packages and does not know about cursors or deletion.

## Invariants

- The three dependency categories remain separate and deterministically ordered.
- Candidate, package-asset expansion, and total-record limits are enforced before output grows unbounded.
- Registry gathering or a serial change marks all registry categories stale.
- Package dependency evidence remains package-granular.

## Verification

Run the Python contract suite, the asset-reference Automation Test, the full UnrealMCP Automation suite, and normal plus forced-unity Editor builds.
