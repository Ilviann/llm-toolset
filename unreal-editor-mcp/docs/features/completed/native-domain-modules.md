---
feature_id: native-domain-modules
status: completed
depends_on:
  - asset-family-conformance
released_in: "0.43.0"
---

# `native-domain-modules` — Native domain module boundaries

**Outcome:** Native feature ownership follows stable domain modules so new asset
families add dependencies and implementation only to their owning domain.

**Depends on:**

- [`asset-family-conformance`](asset-family-conformance.md)

### Implementation

- Retained `UnrealMCP` as the stable host and companion-facing module. It owns
  authentication, transport, lifecycle, the companion API, the operation
  ledger, and frozen command composition.
- Added `UnrealMCPAssetCore`, `UnrealMCPBlueprint`, `UnrealMCPUMG`, and
  `UnrealMCPContent` editor modules. Each owns its services and Automation
  Tests and registers its commands, capabilities, limits, and asset families
  through the typed domain registrar.
- Added explicit deterministic built-in loading before family freeze and bridge
  startup. Every domain descriptor has `LoadingPhase: None`; the host is the
  sole loader and fails closed on missing, duplicate, invalid, or late
  contributions.
- Preserved the external plugin identity, command order and schemas, companion
  API v1, startup behavior, Python catalog, and installed package layout while
  packaging all five base-plugin binaries.

### Verification and completion evidence

- Source contracts verify descriptor loading phases and order, exact command
  ownership, a domain-neutral host, dependency direction, and the five-module
  base package fixture.
- UE 5.8 Windows adaptive, forced-unity, and non-unity builds compile the
  domain modules independently; native Automation and complete headless
  integration cover unchanged runtime behavior.
- Base, GAS, and CommonUI Win64 packaging apply the existing descriptor and
  binary gates. A representative incremental build relinks only affected
  modules; the packaging build supplies the clean-build release gate.
- macOS native verification remains preferred follow-up work. Linux remains
  outside the supported and verified scope.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
