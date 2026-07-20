# MCP presentation and stdio transport

## Purpose

Present the tool API as newline-delimited JSON-RPC over stdio, negotiate MCP initialization, encode tool results, and keep protocol output isolated from diagnostics.

## Owned source

- `godot_editor_mcp/stdio.py` — line transport, JSON-RPC envelopes, text/image result encoding, stderr diagnostics.
- `godot_editor_mcp/server.py` — MCP lifecycle, request validation, mode-filtered listing/calling, shutdown cancellation.

## Dependencies

Uses tool policy and dispatch for schemas and execution, and the shared domain-error model for bounded tool failures. It does not know editor-side service details. The CLI injects the dispatcher.

## Invariants

- Stdout contains only compact protocol messages.
- Each input line contains one JSON-RPC object.
- Explicit non-object `params` and tool `arguments` are rejected.
- Calls outside the active mode are rejected even if a client cached an older list.
- Expected domain failures become tool errors; unexpected programming exceptions remain internal errors.
- PNG results are emitted only from validated `ToolImageResult` values.

## Change and verification guide

Changes to MCP versions, JSON-RPC validation, result encoding, or lifecycle belong here. Review the types in `docs/types/mcp-presentation/`, the tool API component, and stdout/stderr integration coverage. Run `tests.test_server` and `tests.test_stdio` plus the full Python suite.
