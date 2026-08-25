> **Note:** All code and documentation in this repository were created with huge help from ChatGPT.

# LLM Toolset

A collection of lightweight, offline-first tools and reusable skills for local
LLM workflows on macOS, Linux, and Windows. The workspace primarily contains MCP
servers for LM Studio and favors small, dependency-free processes, bounded
output, and simple interfaces. Individual projects target different model sizes
and execution environments, as described below.

## Projects

- [`rooted-files-mcp`](rooted-files-mcp/README.md): a root-confined, text-only
  filesystem MCP server designed for small, locally hosted LLMs.
- [`markdown-mcp`](markdown-mcp/README.md): a root-confined MCP server for
  exact Markdown section reads and optional atomic section editing.
- [`godot-editor-mcp`](godot-editor-mcp/README.md): an authenticated localhost
  bridge for controlling the Godot 4.7 editor, usable with both small local LLMs
  and large online LLMs. Its
  [release history](godot-editor-mcp/HISTORY.md) and
  [planned features](godot-editor-mcp/ROADMAP.md) are documented separately.
- [`unreal-editor-mcp`](unreal-editor-mcp/README.md): an authenticated,
  version-matched bridge for Unreal Engine 5.8+ editor workflows. Its large tool
  surface and context requirements make it best suited to large, locally hosted
  offline LLMs.

Each project contains its own setup, usage, and test instructions. The tools are
designed to run locally without cloud services, telemetry, accounts, or runtime
downloads.

The implementations support Python 3.10 or newer. macOS is the currently
verified development platform; native Linux and Windows validation is pending.
Each project README includes platform-specific commands and LM Studio examples.

## Internal tooling

Run the dependency-free documentation linter from the repository root after changing Markdown documentation:

```sh
python internal/tools/lint_docs.py
```

Its standard-library tests run with:

```sh
python -m unittest discover -s internal/tools/tests -v
```

## Skills

- [`maintain-project-documentation`](skills/maintain-project-documentation/SKILL.md):
  keeps implementation documentation aligned with verified repository behavior,
  including documentation indexes and Git-backed cache markers. It is oriented
  toward large LLM agents.

Each skill includes its instructions and agent-facing metadata in its own
directory.
