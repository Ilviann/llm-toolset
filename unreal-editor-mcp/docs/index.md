# Unreal Editor MCP developer documentation

This directory is the entry point for implementation knowledge. Start here before changing application source. Executable source, package/plugin metadata, behavioral tests, and the runtime `capabilities` response remain authoritative for behavior; these documents explain how those sources fit together and which contracts must change together.

## Top-level contents

- [`workflow.md`](workflow.md) — feature implementation workflow, documentation update rules, and source-of-truth policy.
- [`architecture/`](architecture/index.md) — component boundaries, ownership, dependencies, invariants, and verification guidance. Each implemented component has one file.
- [`types/`](types/index.md) — custom data types, wire records, collaborator protocols, and reusable function libraries, grouped by owning component.
- [`draft.md`](draft.md) — pre-implementation product requirements and proposed tool surface.
- [`notice.md`](notice.md) — requirements-stage technical notes, critiques, and unresolved design considerations.

The last two files are planning inputs, not executable behavior contracts. Planned phases remain in [`../ROADMAP.md`](../ROADMAP.md). User installation and operation guidance and released changes belong in `../README.md` and `../HISTORY.md` once Phase 1 creates them.
