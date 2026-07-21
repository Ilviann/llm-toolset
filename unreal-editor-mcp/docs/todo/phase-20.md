# Phase 20 — Cross-platform qualification and stable release

**Outcome:** The complete supported Blueprint-authoring feature set is packaged, documented, and release-qualified on native macOS and Windows without depending on optional lifecycle or build tools.

### Implementation

- Build and package the exact Python/plugin pair on native macOS and Windows with Unreal 5.8. Add only source-evidenced compatibility fixes behind narrow platform or Unreal-version adapters.
- Run the complete Python, Unreal Automation, cross-process bridge, Actor, framework-family, block-replacement, restart-readback, operation-reconciliation, and preservation suites on both platforms.
- Re-run the security audit: credential faults, bad-token isolation, loopback-only listening, discovery secrecy, request bounds, timeouts, duplicate ownership, operation-ledger isolation, and shutdown cleanup.
- Require identical published schemas, stable errors, limits, versions, operation semantics, family capabilities, and core Blueprint results. Keep filesystem and process differences out of normal contracts.
- Produce offline-installable artifacts and verify a clean-machine installation without accounts, cloud services, telemetry, network downloads, or generated test fixtures.
- Record exact platform, Unreal patch, compiler/toolchain, package format, performance/context measurements, native results, and known limitations.

### Release-candidate checkpoints

- RC1 requires packaged macOS and Windows artifacts, the complete native suites, exact normal-contract parity, and the final security audit.
- RC2 requires clean-machine offline installations, package-content and license audits, final documentation, and acceptance workflows run from packaged artifacts.
- Do not publish the stable tag until both checkpoints pass without contract changes; any contract change invalidates the affected checkpoint.

### Verification

- Run clean-project and existing-project acceptance workflows from packaged artifacts on both platforms.
- Verify metadata, runtime capabilities, README examples, history, package contents, licenses, generated-state exclusions, and exact Python/plugin version matching from executable contracts.
- Keep Linux portability branches unit tested and documented; do not claim native Linux qualification.

### Documentation and completion gate

- Publish complete installation, upgrade, offline preparation, troubleshooting, security, recovery, compatibility, and known-limitations documentation.
- Publish the first stable-tagged release only when both native platform suites and both clean-machine offline installations pass. A later major-version promotion remains a separate explicit decision.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
