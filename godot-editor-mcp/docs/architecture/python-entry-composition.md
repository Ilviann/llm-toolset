# Python entry and composition

## Purpose

Start the stdio MCP process, parse configuration, construct concrete collaborators, and preserve supported executable entry points.

## Owned source

- `server.py` — legacy executable shim.
- `godot_editor_mcp/__init__.py` — package identity and authoritative Python version.
- `godot_editor_mcp/__main__.py` — `python -m godot_editor_mcp` entry.
- `godot_editor_mcp/cli.py` — argument parsing and composition root.

## Dependencies

Composition creates the MCP presentation, tool dispatcher, local asset/process services, bridge client, and operation waiter. It may depend on every Python runtime component, but lower-level components must not depend on the CLI. The lazy compatibility wrappers in `godot_editor_mcp/server.py` are the only deliberate reverse edge.

## Invariants

- The root shim and module entry delegate to the same CLI behavior.
- Project, mode, port, and import-root validation occurs before stdio serving.
- The package and installed Godot plugin are an exact-version pair.
- No third-party runtime dependency or network/runtime download is introduced.

## Change and verification guide

Update this component for new process-level configuration or collaborator construction. Also review tool policy, launcher/bridge construction, package metadata, CLI examples, and release-contract tests. Run `tests.test_server`, `tests.test_launcher`, and `tests.test_contracts`; run the full Python suite for behavior changes.
