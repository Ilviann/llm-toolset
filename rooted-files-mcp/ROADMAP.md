# Rooted Files MCP Roadmap

Feature identifiers are stable names, not execution indexes. Unfinished features
may be implemented and completed in any order once every direct dependency in
their description is complete. The checklist retains a completed feature only
while an unfinished feature directly depends on it; completed feature documents
remain available under [`docs/features/`](docs/features/index.md).

There are currently no active feature requests.

## Support tooling

Support-tooling features change repository utilities without changing Rooted
Files MCP runtime functionality or triggering application version changes.

- [x] [`mcp-settings-text-preview` — Host configuration text snippets](docs/features/completed/mcp-settings-text-preview.md) — Replace the per-field Codex preview with copyable LM Studio `mcp.json` and ChatGPT Codex `config.toml` snippets.
  - Depends on:
    - `mcp-definition-gui`

## Native platform test backlog

Feature checkboxes record implementation completion. This section separately
lists completed features that have not yet passed their applicable native
platform verification.

- macOS:
  - None
- Windows:
  - `markdown-read`
  - `mcp-definition-gui`
- Linux:
  - `markdown-read`
  - `mcp-definition-gui`
