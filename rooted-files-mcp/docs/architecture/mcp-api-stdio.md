# MCP API, JSON-RPC, and stdio

## Purpose

Publish a compact permission-filtered tool catalog, negotiate supported MCP protocols, validate and dispatch JSON-RPC requests, encode safe tool errors, and keep stdout protocol-clean.

## Owned source

- Tool catalog, permission subsets, `build_tools`, `MCPServer`, and stdio loop portions of `rooted_files_mcp/server.py`.

## Dependencies

Uses package version, immutable settings, `RootedFilesystem`, and safe configuration/filesystem errors. It is contract-coupled to README tool schemas/context guidance and package entry/version metadata.

## Invariants

- The public surface remains five focused tools with stable compact schemas.
- `READ_TOOLS`, `WRITE_TOOLS`, catalog names, and dispatch branches remain synchronized.
- Disabled tools are omitted from `tools/list` and rejected if called directly.
- Notifications receive no response; malformed JSON/requests/params use JSON-RPC errors.
- Expected file/type/missing-argument failures become MCP tool errors without terminating the process.
- Unexpected request failures are reported on stderr and become bounded internal errors.
- Each output line is compact JSON and stdout contains no diagnostics.

## Known pressure

Catalog, request handling, transport, and CLI composition currently share one small file. If growth warrants an authorized split, keep data-only schemas, dispatch, stdio, and CLI narrow; preserve compatibility exports and avoid framework dependencies.

## Change and verification guide

Update schema, dispatch, permission sets, README tool guidance, and tests together. Run all `tests.test_server` cases and relevant filesystem/configuration tests. Subprocess coverage is required for transport or startup changes.
