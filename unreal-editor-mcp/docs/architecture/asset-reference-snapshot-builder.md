# Asset reference snapshot builder

## Ownership

`FUnrealMCPAssetReferenceSnapshotBuilder` owns one complete in-memory capture: target resolution, scanner sequencing, stable record ordering, scan aggregation, and the deterministic 40-hex snapshot fingerprint. Its source is `Private/UnrealMCPAssetReferenceSnapshotBuilder.{h,cpp}`; the shared snapshot record is in `Private/UnrealMCPAssetReferenceTypes.h`.

## Dependency direction

The service supplies the starting and current registry serials. The builder depends on the target resolver and both scanners, but not on request parsing, cursor retention, or deletion. The deletion service receives its output only through the facade.

## Invariants

- A capture is immutable after construction and includes exact target metadata, all four scan statuses, stable records, and the observed registry serial.
- Snapshot identity covers target state, scan counts/statuses, open-editor count, and every record fingerprint.
- Capture does not retain a cursor or mutate editor/content state.

## Verification

Run the Python contract suite, asset-reference and asset-deletion Automation Tests, headless integration, and normal plus forced-unity Editor builds.
