# Godot Editor MCP developer documentation

This directory is the entry point for implementation knowledge. Start here before changing application source. The executable source, package/plugin metadata, and runtime `capabilities` response remain authoritative for behavior; these documents explain how those sources fit together and which contracts must change together.

## Top-level contents

- [`workflow.md`](workflow.md) — feature implementation workflow, documentation update rules, and source-of-truth policy.
- [`architecture/`](architecture/index.md) — component boundaries, ownership, dependencies, invariants, and verification guidance. Each component has one file.
- [`types/`](types/index.md) — custom data types, wire records, collaborator protocols, and reusable function libraries, grouped by owning component.

User installation and operation guidance remains in [`../README.md`](../README.md). Released changes and planned feature work remain in [`../HISTORY.md`](../HISTORY.md) and [`../ROADMAP.md`](../ROADMAP.md).
