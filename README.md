# LLM Toolset

A collection of lightweight, offline-first tools for local LLM workflows on
macOS, primarily MCP servers for LM Studio. The workspace favors small,
dependency-free processes, bounded output, and simple tool interfaces that work
well with local models and limited context windows.

## Projects

- [`rooted-files-mcp`](rooted-files-mcp/README.md): a root-confined, text-only
  filesystem MCP server.
- [`godot-editor-mcp`](godot-editor-mcp/README.md): an authenticated localhost
  bridge for controlling the Godot 4.7 editor.

Each project contains its own setup, usage, and test instructions. The tools are
designed to run locally without cloud services, telemetry, accounts, or runtime
downloads.
