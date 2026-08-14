# Asset-family conformance

## Ownership

The repository-only conformance component owns reusable fixture declarations and
common gates for asset-family verification. Native fixtures live in
`Private/Tests/UnrealMCPAssetFamilyConformance.h`; Python, packaging, and live
bridge fixtures live in `scripts/asset_family_conformance.py`. They are test and
support-tool inputs only and do not participate in runtime catalog composition,
native dispatch, schemas, capabilities, or companion admission.

## Dependency direction

Conformance fixtures depend on the frozen native asset-family registry, the
asset-authoring kernel, the immutable Python catalog, generic packaging services,
and the production bridge client. Runtime components never depend on conformance
fixtures. Family-specific tests keep semantic assertions, while the common
runners own identity, capability, deterministic encoding, snapshot, selector,
paging, bounds, unavailable-state, cleanup, stale-state, transaction recovery,
persistence, replay/lost-response, restart read-back, and preservation checks.

The native matrix applies independent inspection-only, creation-only, edit-only,
combined, and missing-dependency descriptors, then checks the built-in
`core_blueprint` and `neutral_asset` descriptors and the Game Data authoring
lifecycle. Python fixtures cover Asset, Blueprint, Widget, and Game Data
publication plus the admitted and dormant branches of the test companion.
Packaging fixtures cover the base plugin and test companion. Cross-process
fixtures are used by the built-in Blueprint, Widget, Game Data, and test-companion
scenarios.

## Invariants

- A fixture declares one stable family identity and only the capabilities and
  boundary expectations it supports; unsupported gates fail closed.
- Catalog composition is deterministic, does not mutate static schemas, removes
  mutation branches in readonly mode, and removes exact tools or companion
  branches when native requirements are unavailable or rejected.
- Native adapter fixtures use bounded document, selector, and snapshot builders.
  Restart-equivalent registries must produce the same fingerprint.
- Authoring fixtures use exact kernel targets and prove failed-creation cleanup,
  stale rejection before semantics, failed-postcondition rollback, persistence,
  Undo/Redo, exact read-back, and unrelated-state preservation.
- Cross-process inspection repeats the same production command, requires one
  lowercase 40-character snapshot, compares deterministic bounded output, and
  retains that snapshot through selector/page variants and restart read-back.
- Package fixtures use repository-owned plugin identities and exact descriptor
  dependency edges; packaged output must retain the source contract and contain
  the declared module binary.

## Verification

Run `python -m unittest tests.test_asset_family_conformance -v`, then the complete
Python suite. Run `UnrealMCP.AssetFamilies.ConformanceMatrix` together with all
native Automation Tests, adaptive and true forced-unity UE 5.8 builds, the full
headless cross-process workflow, and base plus test-companion Win64 packaging.

[Types and fixtures](../types/asset-family-conformance/index.md) · [Architecture index](index.md)
