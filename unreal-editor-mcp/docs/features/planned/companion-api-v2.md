---
feature_id: companion-api-v2
status: planned
depends_on:
  - companion-plugins
  - native-domain-modules
  - python-asset-family-catalog
released_in: null
---

# `companion-api-v2` — Typed asset-family companion API

**Outcome:** One exact companion API v2 supports typed asset classification, inspection, creation, editing, read-back, persistence, and capabilities without exposing JSON or transport ownership to companions.

**Depends on:**

- [`companion-plugins`](../completed/companion-plugins.md)
- [`native-domain-modules`](../completed/native-domain-modules.md)
- [`python-asset-family-catalog`](../completed/python-asset-family-catalog.md)

### API contract

- Include the complete foreseeable seam in v2: family classification, root/selector inspection, snapshot contribution, creation without a target, existing-target editing, stable nested identities, postcondition read-back, persistence requirements, typed capabilities, and limits.
- Remove `FJsonObject` from companion handler interfaces. The host retains schemas, exact target resolution, access policy, authentication, dispatch, ledger, transactions, persistence authority, response encoding, capability composition, and collision policy.
- Keep companions independently versioned and require exact descriptor/compiled/base API equality. Reject API v1 after migration; do not accept ranges or mixed installations.

### Migration impact and approval gate

- Migrate `UnrealMCPGAS`, `UnrealMCPCommonUI`, and `UnrealMCPTestCompanion` together. Update base and companion descriptors/constants, Python catalogs, deployment, packaging, fixtures, release tests, examples, and documentation.
- Before implementation, present a refreshed impact report naming every then-current companion and fixture and obtain explicit permission for the global API/schema migration.
- Apply a minor base version bump. Apply at least a patch bump to every API-only companion migration and a minor bump wherever the same feature adds substantive behavior.

### Verification and completion gate

- Test missing/mismatched descriptors and binaries, stale packages, load ordering, collisions, all capability combinations, unavailable Engine plugins, base-only operation, and owner-checked shutdown.
- Build, package, and run complete native/Python/headless suites for the base and every migrated companion on Windows.
- Complete only when no v1 companion registers and the full companion set passes as one exact API-v2 release state.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
