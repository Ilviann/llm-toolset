# LLM Toolset

A collection of lightweight, offline-first tools for local LLM workflows on
macOS, Linux, and Windows, primarily MCP servers for LM Studio. The workspace favors small,
dependency-free processes, bounded output, and simple tool interfaces that work
well with local models and limited context windows.

## Projects

- [`rooted-files-mcp`](rooted-files-mcp/README.md): a root-confined, text-only
  filesystem MCP server.
- [`godot-editor-mcp`](godot-editor-mcp/README.md): an authenticated localhost
  bridge for controlling the Godot 4.7 editor. Its
  [release history](godot-editor-mcp/HISTORY.md) and
  [unimplemented features](godot-editor-mcp/TODO.md) are documented separately.

Each project contains its own setup, usage, and test instructions. The tools are
designed to run locally without cloud services, telemetry, accounts, or runtime
downloads.

The implementations support Python 3.10 or newer. macOS is the currently
verified development platform; native Linux and Windows validation is pending.
Each project README includes platform-specific commands and LM Studio examples.
