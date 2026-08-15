---
feature_id: windows-deployment-enhanced-input-preview
status: completed
depends_on:
  - windows-deployment-validation-fixes
  - enhanced-input-assets-inspect
  - windows-deployment-codex-preview
released_in: null
release_track: support-tooling
---

# `windows-deployment-enhanced-input-preview` — Complete companion deployment and settings preview

**Outcome:** Windows users can independently deploy every released production companion and copy sample LM Studio `mcp.json` or ChatGPT Codex `config.toml` entries from one settings preview.

**Implementation status:** Completed as unversioned support tooling without changing MCP runtime behavior, native plugin metadata, companion APIs, or semantic versions.

**Depends on:**

- [`windows-deployment-validation-fixes`](windows-deployment-validation-fixes.md)
- [`enhanced-input-assets-inspect`](enhanced-input-assets-inspect.md)
- [`windows-deployment-codex-preview`](windows-deployment-codex-preview.md)

### Deployment behavior

- Independent default-off checkboxes select `UnrealMCPGAS`, `UnrealMCPCommonUI`, and `UnrealMCPEnhancedInput`; the disposable `UnrealMCPTestCompanion` fixture is never offered.
- Enhanced Input uses its fixed descriptor and the base descriptor dependency, participates in the same project or Engine destinations, project enablement, stale-state checks, verification, and atomic rollback, and expands the transaction bound to base plus three production companions.
- The window contains exactly two output tabs: **Build log output** and **MCP settings preview**.
- The settings preview derives both a complete LM Studio `mcp.json` entry and a ChatGPT Codex `config.toml` entry from the same validated command and ordered arguments. It replaces the former per-field Codex setup tab and does not write either host configuration.

### Verification

- Focused deployment tests cover the Enhanced Input descriptor/dependency command, independent selection, deterministic ordering, destinations, project enablement, four-plugin transaction, strict Boolean input, and both settings formats.
- The full Python suite and documentation linter cover existing packaging, deployment, configuration, and indexed-documentation contracts.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
