# Rooted Files MCP developer documentation

This directory is the implementation-knowledge entry point. Start here before changing application source. Executable source, package metadata, tool schemas, and behavioral tests remain authoritative for behavior; these documents explain component ownership, dependencies, invariants, and the contracts that must change together.

## Top-level contents

- [`workflow.md`](workflow.md) — feature implementation workflow, documentation update rules, and source-of-truth policy.
- [`architecture/`](architecture/index.md) — component boundaries, dependencies, security invariants, change guidance, and verification. Each component has one file.
- [`types/`](types/index.md) — custom data types, policies, records, and reusable function libraries grouped by owning component.

User installation and operation guidance remains in [`../README.md`](../README.md). Planned feature work remains in [`../ROADMAP.md`](../ROADMAP.md).
