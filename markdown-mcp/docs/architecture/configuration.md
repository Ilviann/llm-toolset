# Startup configuration

## Purpose

Resolve the required served root once and select the immutable read-only or
writable runtime mode.

## Owned source

- `markdown_mcp/configuration.py` — `Settings`, argument parser construction,
  root validation, and `--writable` handling.
- `markdown_mcp/__init__.py` and `pyproject.toml` — synchronized release version.

## Boundaries and invariants

The component accepts exactly one required root and one optional writable flag.
It resolves the root strictly at startup and requires an existing directory.
There is no persisted configuration, workspace indirection, account, network,
permission list, hidden-file switch, or mutable runtime state.

The settings record is shared read-only by path, filesystem, and MCP catalog
components. Enabling the writable catalog never weakens path or filesystem
validation.

## Verification

`tests/test_configuration.py` covers required arguments, invalid roots, the
writable flag, and immutability. Release checks synchronize every version
source and initialization output.
