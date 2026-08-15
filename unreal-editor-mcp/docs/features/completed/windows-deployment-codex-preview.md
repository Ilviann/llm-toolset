---
feature_id: windows-deployment-codex-preview
status: completed
depends_on:
  - windows-deployment-install-modes
released_in: null
release_track: support-tooling
---

# `windows-deployment-codex-preview` — Codex deployment configuration preview

**Outcome:** Windows users can copy the exact server name, command, and ordered arguments into the ChatGPT Codex app's STDIO MCP server form after deployment.

**Implementation status:** Completed as unversioned support tooling without changing the MCP runtime, native bridge, companion API, installation transaction, or plugin version.

The later [`windows-deployment-enhanced-input-preview`](windows-deployment-enhanced-input-preview.md) feature replaces the per-field Codex tab with a combined LM Studio `mcp.json` and ChatGPT Codex `config.toml` text preview.

**Depends on:**

- [`windows-deployment-install-modes`](windows-deployment-install-modes.md)

### Deployment behavior

- One validated launch definition owns the stable `unreal-editor` name, current Python executable, checkout `server.py`, selected `.uproject`, and optional writable and editor-lifecycle arguments.
- The deployment GUI presents that definition in an **LM Studio JSON** tab and a **ChatGPT Codex STDIO** tab after successful installation.
- The Codex tab exposes the name, command, and each ordered argument as separate readonly fields with independent copy buttons. Empty optional argument rows cannot be copied.
- Neither preview writes host configuration or includes the Unreal bridge token.

### Verification

- Focused Python deployment coverage verifies that the LM Studio JSON and Codex fields derive from the same launch definition for readonly, writable, lifecycle-only, and combined configurations.
- The documentation linter verifies the indexed feature, architecture, type, and user guidance.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
