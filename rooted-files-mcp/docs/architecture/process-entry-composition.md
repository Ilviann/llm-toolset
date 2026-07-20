# Process entry and composition

## Purpose

Provide compatible script, package-module, and installed-command entry points; parse CLI overrides; resolve configuration; and compose the rooted filesystem with MCP stdio serving.

## Owned source

- `server.py` — direct script launcher.
- `rooted_files_mcp/__init__.py` — package identity and authoritative runtime version.
- `rooted_files_mcp/__main__.py` — `python -m rooted_files_mcp` entry.
- CLI and `run` portions of `rooted_files_mcp/server.py` — arguments, settings loading, construction, and startup errors.

## Dependencies

Composition depends on configuration/effective policy, the rooted filesystem facade, and MCP/stdio handling. Lower components must not depend on entry points. Package entry metadata in `pyproject.toml` is a release contract.

## Invariants

- All entry paths resolve to the same `main()` behavior.
- A positional root remains the backward-compatible trusted-root form.
- Configuration-only startup resolves its workspace before serving.
- CLI values override INI values, which override built-in defaults.
- Startup failures are concise stderr diagnostics; stdout remains protocol-only.
- Runtime uses only the Python standard library.

## Change and verification guide

Review CLI help, README launch/configuration examples, package entry metadata, version synchronization, and subprocess tests for any process-level change. Run `tests.test_configuration` and the startup portions of `tests.test_server`, followed by the complete suite.
