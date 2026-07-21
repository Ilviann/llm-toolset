# Phase 14 — Native Windows Actor beta

**Outcome:** The hardened Actor Blueprint workflow passes its first native Windows qualification and is ready for a supervised cross-platform beta.

Native Windows build preparation and environment setup may proceed in parallel with late Phase 13 work after the model-facing contracts from Phase 12 are frozen. Qualification and release remain ordered after Phase 13.

### Implementation

- Build the plugin and run the complete Actor acceptance workflow on native Windows.
- Fix only source-evidenced platform differences behind narrow compatibility or platform adapters with tests.
- Keep published schemas, errors, limits, operation semantics, Blueprint results, and exact Python/plugin matching identical across macOS and Windows.
- Add Windows setup and troubleshooting material without weakening the offline-first or security contracts.

### Verification

- On native Windows, verify credentials and permissions, loopback binding, discovery, paths and casing, plugin loading, Game-thread dispatch, component/member/graph transactions, compilation, saving, restart read-back, and exact model-facing contracts.
- Run the complete Python, Unreal Automation, cross-process, clean-project, and existing-Blueprint beta suites.
- Record peak request/response bytes, retained state, startup time, schema size, and typical operation latency on the Windows qualification host and compare them with the macOS baseline.

### Documentation and completion gate

- Complete Windows installation, configuration, troubleshooting, limits, recovery, and end-to-end workflow documentation.
- Publish the Actor Blueprint beta only when the native macOS acceptance workflows remain green and the defined native Windows beta suite passes.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
