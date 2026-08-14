# Asset-family conformance fixtures and gates

## Native fixtures

`FFixture` declares a stable family ID, exact target class, independent expected
inspection/creation/editing capabilities, a descriptor factory, dependency
availability, and whether its adapters can be exercised directly. `RunFixture`
registers and freezes two equivalent registries, compares fingerprints and family
identity, checks every capability and unavailable-state error, and exercises
bounded adapters when requested.

`FAuthoringFixture` supplies exact asset construction, semantic read/write,
unrelated-state read-back, and retirement callbacks. `RunAuthoringFixture` owns
the shared failed-creation cleanup, stale edit, failed-postcondition rollback,
persistence, exact snapshot, unrelated-content, and Undo/Redo gates. The family
callback remains responsible only for typed semantic state.

## Python and package fixtures

`CatalogFamilyFixture` contains one catalog family identity, ordered tool access
and result-policy expectations, optional companion identity, and exact
contribution keys. `verify_catalog_family` checks immutable deterministic
composition, readonly filtering, unavailable native commands, and companion
readiness/schema rejection without duplicating schemas.

`PackageFamilyFixture` binds one family identity to a repository `PluginIdentity`
and its exact module names. Source verification checks descriptor modules and
enabled repository dependency edges. Packaged verification composes the existing
package contract with exact module-binary presence.

## Cross-process fixtures

`CrossProcessFamilyFixture` declares the production command, bounded arguments,
exact nested identity fields, optional selector or paging variants, and the small
set of opaque result fields excluded from byte-for-byte determinism checks.
`verify_cross_process_family` repeats the request, validates deterministic
encoding and a lowercase SHA-1 snapshot, checks identity and variants, and can
compare a caller-owned preservation probe. `verify_restart_read_back` adds exact
persisted snapshot verification. `verify_recovered_mutation` validates a retained
committed result after replay or a deliberately lost response.

[Architecture](../../architecture/asset-family-conformance.md) · [Type index](../index.md)
