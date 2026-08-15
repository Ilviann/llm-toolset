# Process entry and composition

## Purpose

Provide compatible script, package-module, and installed-command entry points;
parse CLI overrides; resolve configuration; compose the rooted filesystem with
MCP stdio serving; and generate host launch definitions through an optional
desktop helper.

## Owned source

- `server.py` — direct script launcher.
- `rooted_files_mcp/__init__.py` — package identity and authoritative runtime version.
- `rooted_files_mcp/__main__.py` — `python -m rooted_files_mcp` entry.
- CLI and `run` portions of `rooted_files_mcp/server.py` — arguments, settings loading, construction, and startup errors.
- `scripts/generate_mcp_definition.py` — tkinter definition generator for local
  MCP hosts.
- `scripts/generate_mcp_definition.sh` — POSIX GUI launch helper.
- `scripts/generate_mcp_definition.cmd` — Windows GUI launch helper.

## Dependencies

Composition depends on configuration/effective policy, the rooted filesystem facade, and MCP/stdio handling. Lower components must not depend on entry points. Package entry metadata in `pyproject.toml` is a release contract.

## Invariants

- All entry paths resolve to the same `main()` behavior.
- A positional root remains the backward-compatible trusted-root form.
- Configuration-only startup resolves its workspace before serving.
- CLI values override INI values, which override built-in defaults.
- `--mode {standard,markdown}` participates in the same precedence contract.
- Startup failures are concise stderr diagnostics; stdout remains protocol-only.
- Runtime uses only the Python standard library.
- The helper validates and resolves the selected root, current Python
  executable, and repository launcher before presenting configuration.
- Generated definitions use the direct script launcher, an explicit mode, and
  the same ordered arguments in LM Studio `mcp.json` and ChatGPT Codex
  `config.toml` snippets.
- The helper displays configuration but never writes host configuration files.
- The shell launcher resolves its own directory and invokes the adjacent helper
  with `python3`.
- The Windows launcher resolves the adjacent helper from its own directory,
  invokes it with `python`, and preserves the Python process exit code.

## Change and verification guide

Review CLI help, README launch/configuration examples, package entry metadata,
version synchronization, generator tests, and subprocess tests for any
process-level change. Run `tests.test_configuration`,
`tests.test_mcp_definition_gui`, and the startup portions of
`tests.test_server`, followed by the complete suite.
