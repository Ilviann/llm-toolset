# Filesystem validation and atomic editing

## Purpose

Read and validate bounded Markdown bytes, apply pure transformations, and
replace existing files without partial content or authority drift.

## Owned source

- `markdown_mcp/filesystem.py` — complete reads, strict UTF-8/BOM state, NUL and
  resource checks, parser integration, write permission, source snapshots,
  same-directory temporary files, fsync, revalidation, replacement, and cleanup.

## Read flow

The filesystem asks the path resolver for an authorized target, compares
pre-open and opened file identities, reads at most 256 KiB plus one detection
byte, rejects NULs, and decodes the complete payload as strict UTF-8. A leading
BOM is retained as snapshot state and omitted from logical parser/model text.
Semantic string and structured-list results are independently UTF-8 bounded.

## Write flow

Writable operations validate their inputs, load a complete snapshot, calculate
one in-memory transformation, restore the optional BOM, and reject an edited
payload above 256 KiB. A temporary file is created beside the resolved target,
written, flushed, fsynced, and assigned the original mode.

Immediately before `os.replace`, the component resolves the original plain path
again, confirms the target and parent, rereads and validates the authorized
file, and compares identity, mode, and exact source bytes with the snapshot.
Every failure removes the temporary file. A successful replacement fsyncs the
parent where the platform supports it.

## Security and concurrency invariants

Write permission is checked below MCP dispatch and before mutation path access.
File creation is impossible because initial and repeated resolution are strict.
Concurrent content, file identity, mode, target, parent, type, suffix, or root
authority changes cause the edit to fail without replacing the current file.

## Verification

`tests/test_filesystem.py` covers UTF-8, BOM, NUL, limits, hidden and absolute
paths, every mutation, mode and format preservation, no-op behavior, simulated
concurrent changes, replacement failure, and temporary cleanup.
