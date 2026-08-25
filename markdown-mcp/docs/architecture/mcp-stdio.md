# MCP API and stdio process

## Purpose

Expose the compact effective tool catalog, validate model arguments, dispatch
to the filesystem facade, and speak LM Studio-compatible newline-delimited
JSON-RPC without contaminating stdout.

## Owned source

- `markdown_mcp/server.py` — tool schemas, catalog filtering, argument
  validation, MCP methods, dispatch, structured listing results, protocol
  errors, stdio configuration, and process loop.
- repository `server.py` and `markdown_mcp/__main__.py` — script and module
  launchers.

## Catalog and dispatch

Read-only mode publishes `read_markdown` and `list_sections`. Writable mode adds
`overwrite_section`, `append_section`, `set_front_matter`, and
`delete_section`. Schemas reject additional properties and bound paths, titles,
levels, and body character counts; the filesystem repeats byte and semantic
validation. Known mutation tools omitted by the effective mode are rejected
before dispatch.

`list_sections` returns its documented object in `structuredContent` and a
compact JSON text representation for compatible hosts. Other tools return text
content. Expected failures are MCP tool errors. Malformed JSON-RPC uses standard
protocol errors, and unexpected failures return only `Internal error` through
stdout while bounded diagnostics go to stderr.

## Process invariants

Stdin, stdout, and stderr are reconfigured to strict UTF-8; stdout uses LF. The
server accepts the current and compatibility protocol versions, ignores
notifications, emits one compact JSON response per request, and keeps serving
after malformed or unexpected requests.

For release 0.1.0, compact catalog JSON measures 923 UTF-8 bytes read-only and
2,092 bytes writable.

## Verification

`tests/test_server.py` covers schemas, catalogs, dispatch, disabled tools,
initialization, errors, and structured output. `tests/test_subprocess.py` covers
initialization/list/call framing, notifications, parse recovery, writable mode,
protocol-clean output, and Unicode under forced inherited ASCII encoding.
