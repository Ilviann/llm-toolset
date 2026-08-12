---
feature_id: native-domain-modules
status: planned
depends_on:
  - asset-family-conformance
released_in: null
---

# `native-domain-modules` — Native domain module boundaries

**Outcome:** Native feature ownership follows stable domain modules so new asset families add dependencies and implementation only to their owning domain.

**Depends on:**

- [`asset-family-conformance`](asset-family-conformance.md)

### Implementation

- Retain `UnrealMCP` as the stable host and companion-facing module. Extract cohesive asset-core, Blueprint, UMG, and content modules behind the fixed command and family catalogs.
- Add later modules, such as Animation, only with their first real adapter; do not create one module per class or speculative empty modules.
- Explicitly load only repository-owned built-in modules in deterministic order. Keep tests with their owning domains and minimize cross-module exported types.
- Preserve external plugin identity, public companion API v1 until its dedicated migration, startup/shutdown order, packaging layout, and model-facing behavior.

### Verification and completion gate

- Compare clean and representative incremental builds while treating feature isolation, dependency direction, and test ownership as the primary success criteria.
- Run adaptive/forced-unity/non-unity builds, all native Automation, complete headless integration, and base/GAS/CommonUI packaging.
- Complete only when leaf domain changes do not require host or unrelated-domain source changes and clean-build/package regressions remain acceptable.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
