# Unreal Editor MCP developer documentation

This directory is the entry point for implementation knowledge. Start here before changing application source. Executable source, package/plugin metadata, behavioral tests, and the runtime `capabilities` response remain authoritative for behavior; these documents explain how those sources fit together and which contracts must change together.

## Top-level contents

- [`development-environment.md`](development-environment.md) — local Unreal, Xcode, Python, disposable-project, path-configuration, and native verification requirements.
- [`issues.md`](issues.md) — Unreal Engine, macOS launch, HTTPServer lifecycle, and Xcode observations encountered during native implementation.
- [`workflow.md`](workflow.md) — feature implementation workflow, documentation update rules, and source-of-truth policy.
- [`architecture/`](architecture/index.md) — component boundaries, ownership, dependencies, invariants, and verification guidance. Each implemented component has one file.
- [`types/`](types/index.md) — custom data types, wire records, collaborator protocols, and reusable function libraries, grouped by owning component.
- [`todo/`](todo/index.md) — shared roadmap contracts and detailed implementation, verification, documentation, and completion requirements for each phase.
- [`draft.md`](draft.md) — original product requirements and proposed later tool surface.
- [`notice.md`](notice.md) — requirements-stage technical notes and later design considerations.

The last two files are planning inputs, not executable behavior contracts. The concise phase checklist remains in [`../ROADMAP.md`](../ROADMAP.md), with shared roadmap contracts and detailed phase requirements under [`todo/`](todo/index.md). User installation and operation guidance and released changes live in [`../README.md`](../README.md) and [`../HISTORY.md`](../HISTORY.md).
