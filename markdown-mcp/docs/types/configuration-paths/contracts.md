# Configuration and path contracts

## `Settings`

**Source:** `markdown_mcp/configuration.py`

An immutable record containing one canonical existing directory `root` and one
boolean `writable`. `Settings.for_root` resolves and validates the root once;
CLI parsing requires the root and defaults `writable` to false.

## Supported Markdown path

**Source:** `markdown_mcp/paths.py`

`is_markdown_path` accepts only final `.md` and `.markdown` suffixes using
case-insensitive suffix comparison. `split_markdown_fragment` separates only
the final `#` when the complete preceding path already has a supported suffix.
It does not decode either part.

## `ResolvedMarkdownPath`

**Source:** `markdown_mcp/paths.py`

The immutable authorization result retains the original model path, plain path
portion, canonical existing regular path, and optional raw fragment. The record
exists only after requested/resolved suffix, native resolution, and root
confinement checks succeed.

## Resolver and revalidation

`MarkdownPathResolver.resolve` accepts relative or absolute paths, limits path
length, rejects NUL, and optionally forbids fragments. `revalidate` repeats
resolution for a previously authorized plain path and requires the same target
and parent. `PathAccessError` messages contain no resolved host path.

## Host launch definition

**Source:** `scripts/generate_mcp_config.py`

`build_server_definition` resolves an existing Markdown root, Python executable,
and direct `server.py` launcher. It returns a JSON-compatible STDIO definition
whose ordered arguments are the launcher and root followed by `--writable` only
for writable mode.

`format_mcp_json` wraps that definition as the stable `markdown` member of a
complete `mcpServers` object. `format_codex_toml` renders the same command and
arguments in an `[mcp_servers.markdown]` table. The combined preview is
copy-only and does not persist host configuration.
