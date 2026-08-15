---
feature_id: mcp-settings-text-preview
status: completed
depends_on:
  - mcp-definition-gui
released_in: null
release_track: support-tooling
---

# `mcp-settings-text-preview` — Host configuration text snippets

**Outcome:** Operators can copy sample LM Studio `mcp.json` or ChatGPT Codex
`config.toml` entries from one text preview after selecting the served folder
and host mode.

**Implementation status:** Completed as unversioned support tooling without
changing Rooted Files MCP runtime behavior, tool contracts, package metadata,
or semantic version.

**Depends on:**

- [`mcp-definition-gui`](mcp-definition-gui.md)

### Generator behavior

- One validated launch definition owns the Python command and ordered server
  arguments for both snippets.
- The readonly settings preview contains a complete LM Studio `mcp.json`
  `mcpServers.rooted-files` object and a ChatGPT Codex `config.toml`
  `[mcp_servers.rooted-files]` table.
- The combined text preview replaces the separate Codex name, command, and
  argument fields.
- The helper copies only on explicit user action and does not write or merge
  either host configuration file.

### Verification

- Focused generator tests verify the exact Codex table, both settings-preview
  sections, malformed-definition rejection, and the existing launch behavior.
- The full application suite and documentation linter cover the existing
  runtime and indexed-documentation contracts.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
