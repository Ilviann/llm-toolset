# Unreal Editor MCP developer documentation

This directory is the entry point for implementation knowledge. Start here before changing application source. Executable source, package/plugin metadata, behavioral tests, and the runtime `capabilities` response remain authoritative for behavior; these documents explain how those sources fit together and which contracts must change together.

## Top-level contents

- [`development-environment.md`](development-environment.md) — local Unreal, Xcode, Python, disposable-project, path-configuration, and native verification requirements.
- [`pitfalls.md`](pitfalls.md) — confirmed Unreal Engine, platform, launch, lifecycle, and toolchain pitfalls encountered during native implementation.
- [`issues/`](issues/index.md) — numbered records for confirmed errors that remain unfixed.
- [`workflow.md`](workflow.md) — feature implementation workflow, documentation update rules, and source-of-truth policy.
- [`user/`](user/index.md) — detailed installation, operation, and tool-family guides with request examples.
- [`architecture/`](architecture/index.md) — component boundaries, ownership, dependencies, invariants, and verification guidance. Each implemented component has one file.
- [`types/`](types/index.md) — custom data types, wire records, collaborator protocols, and reusable function libraries, grouped by owning component.
- [`features/`](features/index.md) — status-grouped roadmap contracts and detailed implementation, verification, documentation, and completion requirements for each feature.

The concise feature checklist remains in [`../ROADMAP.md`](../ROADMAP.md), with shared roadmap contracts and detailed feature requirements under [`features/`](features/index.md). User installation and quickstart guidance lives in [`../README.md`](../README.md), detailed operational guidance lives under [`user/`](user/index.md), and released changes live in [`../HISTORY.md`](../HISTORY.md).
