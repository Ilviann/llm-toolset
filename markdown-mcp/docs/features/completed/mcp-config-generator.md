---
feature_id: mcp-config-generator
status: completed
depends_on:
  - markdown-sections
released_in: null
release_track: support-tooling
---

# `mcp-config-generator` — Host configuration helper

## Summary

Operators can select the existing Markdown root, choose read-only or writable
access, and copy valid local STDIO configuration for LM Studio or Codex without
manually locating or escaping the Python, launcher, and root paths.

This is support tooling only. It does not change Markdown MCP runtime behavior,
tool contracts, package metadata, or semantic version.

**Depends on:**

- `markdown-sections`

## Helper contract

- Provide a directly runnable, dependency-free Tkinter helper under `scripts/`
  with adjacent Windows and POSIX launch wrappers.
- Resolve and validate the selected root, current Python executable, and direct
  repository server launcher.
- Default to read-only and append exactly `--writable` only when editing is
  selected.
- Render a complete `mcpServers.markdown` JSON definition and a Codex
  `[mcp_servers.markdown]` TOML entry from the same ordered launch definition.
- Copy only on explicit operator action and never write or merge host
  configuration files.
- Fail safely when a required path is missing, inaccessible, or has the wrong
  type.

## Verification

- Unit-test exact read-only and writable arguments, JSON/TOML rendering,
  malformed definitions, invalid roots, and invalid launcher files.
- Launch both generated definitions and verify the exact read-only and writable
  tool catalogs through MCP `tools/list` framing.
- Run the complete Markdown MCP suite and repository documentation linter.

[Back to roadmap](../../../ROADMAP.md) · [Feature index](../index.md)
