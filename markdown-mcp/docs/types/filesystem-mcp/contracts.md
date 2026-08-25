# Filesystem and MCP contracts

## Source and snapshot limits

**Source:** `markdown_mcp/filesystem.py`

`MAX_MARKDOWN_BYTES` is exactly `256 * 1024`. It bounds complete source bytes,
edited bytes including an optional BOM, body input bytes, and semantic result
UTF-8 bytes. A private snapshot contains the authorized resolved record, exact
raw source, BOM-free logical text, BOM state, opened file identity, and source
mode.

## Read operations

`MarkdownFilesystem.read_markdown` returns BOM-free validated logical text or
one exact selected block. `list_sections` forbids fragments and returns the
documented structured object. Both validate the complete source before
selection and enforce semantic result size.

## Mutation operations

All mutation methods require writable settings below dispatch, validate inputs,
load one snapshot, invoke one pure transformation, and atomically replace only
when bytes change. Success text is bounded and stable. `FileAccessError`
contains model-safe policy, validation, limit, concurrency, or I/O messages.

## Atomic replacement state

The temporary target is an exact byte payload in the authorized target's
directory with the original mode. Before replacement, resolver authority,
target and parent, regular type, exact source bytes, identity, and mode must
still match the snapshot. The temporary file is removed on every failure.

## MCP catalogs and results

**Source:** `markdown_mcp/server.py`

`TOOLS` is the single schema catalog. `READ_TOOLS`, `WRITE_TOOLS`, and
`build_tools` derive the effective catalog; `KNOWN_TOOLS` supports direct
disabled-tool rejection. Schemas are closed objects. `list_sections` has an
output schema and returns matching `structuredContent` plus compact JSON text;
other calls return text content. Expected operation failures set `isError`.

## Process protocol

`MCPServer.handle` owns initialization, ping, tools/list, tools/call,
notifications, and JSON-RPC errors. `run` owns line framing, parse recovery,
bounded internal diagnostics, and compact Unicode-preserving responses. All
three stdio streams are strict UTF-8 regardless of inherited locale encoding.
