# Rooted Files MCP

A small, dependency-free MCP server that exposes one folder as `root`. It uses
relative paths, accepts text only, and communicates over stdio.

Programmers and coding agents should start with the indexed
[`docs/`](docs/index.md) knowledge cache before changing source. It maps
component ownership, dependency direction, custom data types, reusable
libraries, security invariants, and the required feature implementation
workflow.

## Platform support

The server supports Python 3.10 or newer on macOS, Linux, and Windows and has no
third-party runtime dependencies. macOS is currently verified; native Linux and
Windows validation is pending. Path confinement uses the host platform's native
path rules, while paths exposed to the model remain relative to the configured
root. Forward slashes are accepted on every supported platform and are the
recommended model-facing form.

## Tools

| Tool | Purpose |
|---|---|
| `list_dir` | List direct entries in a folder; requires read permission |
| `tree` | Show a recursive tree, limited to 100 entries; requires read permission |
| `read_text` | Read a whole UTF-8 text file, a one-based end-inclusive line range, a Markdown heading section, or YAML front matter; requires read permission |
| `write_text` | Create or replace a UTF-8 text file; requires write permission |
| `write_lines` | Atomically replace a one-based, end-inclusive range of UTF-8 text lines; requires write permission |

All paths are relative to the root argument. Absolute paths, `..` escapes, and
symlinks resolving outside root are denied. Known binary/media extensions,
binary signatures, NUL bytes, invalid UTF-8, and files over 5 MiB are denied.
Tools disabled by the effective permissions are omitted from `tools/list` and
direct calls to them are rejected. Hidden-path settings are enforced for every
tool, not only directory output.

The standard-mode `tools/list` payload, including all five descriptions and
input schemas, is 1,328 characters of compact JSON (1,318 characters for the
tool definitions), or roughly 330–465 tokens for common model tokenizers. The
Markdown-mode payload containing only `read_text` is 344 characters (334 for
the definition), roughly 85–120 tokens. Disabling the relevant permission makes
either catalog smaller. This is the tool-schema context cost before system
prompts, the conversation, tool calls, and file contents. Exact usage varies by
model and by how the MCP client represents tool definitions.

When pairing this server with `godot-editor-mcp` for GDScript work, prefer the
Godot server's default `tiny` mode below 8k context. Use `small` only when the
agent also needs Godot asset/import tools; `large` adds the desktop-oriented
selection helper. Expose the closest useful script folder here to keep the
combined schemas and file results bounded.

## Agentic usage by context size

Agentic use is practical when the exposed root and task are kept narrow. The
5 MiB file limit is a safety limit, not a useful context target. Prefer a
Markdown anchor or the ranged form of `read_text`, and use `write_lines` when
the relevant coordinates are known, so only selected text enters model
context. Without a fragment or range arguments, `read_text` returns a whole
file; `write_text` always replaces a whole file.

- **4k context:** Suitable for inspecting a small folder and reading or updating
  one small file. Expose the closest useful subfolder, start with `list_dir`, and
  keep the prompt and expected output short. Avoid broad `tree` calls, large
  files, and multi-file refactors.
- **8k context:** Suitable for a short task across a few small files. Use `tree`
  once for orientation, then read only the relevant files. Split larger changes
  into separate sessions before tool results and rewritten file contents crowd
  out the original instructions.
- **16k context:** Suitable for modest multi-file work and verification, but not
  repository-wide autonomous development. Keep the root scoped, read files on
  demand, and divide large refactors into checkpoints; a single large
  `read_text` result can still consume the window.

## Markdown section access

For `.md` and `.markdown` files, append a generated heading fragment to the
root-relative path:

```text
read_text("docs/setup.md#installation")
read_text("docs/setup.md#example-1")
read_text("docs/setup.md#---")
```

A heading fragment returns the heading source and its complete section,
including nested subsections, stopping before the next heading of the same or a
higher level. ATX headings (`#` through `######`) and Setext headings (`===` or
`---`) are supported with up to three leading spaces. Heading-like content in
backtick/tilde fenced code, four-space/tab-indented code, and leading YAML front
matter is ignored. Selected source is not normalized: `\n`/`\r\n` endings and a
missing final newline remain exact. A leading UTF-8 BOM is omitted, consistently
with whole-file reads.

Generated fragments use this dependency-free GitHub-style algorithm. The
heading's rendered label is approximated by retaining link labels, removing
HTML tags and code ticks, resolving Markdown backslash escapes and HTML
entities, and collapsing whitespace. Unicode letters, numbers, marks, and
symbols retain their authored code points and are lowercased without Unicode
normalization. Punctuation is removed except `-` and `_`; each remaining space
becomes `-`. Punctuation removal can therefore leave adjacent hyphens. Repeated
or colliding fragments receive the first available `-1`, `-2`, and so on
suffix. Fragment matching is exact and case-sensitive, independent of the host
filesystem's case rules. UTF-8 URL percent escapes are decoded once, so either
`#привет-мир` or its percent-encoded equivalent is accepted.

The reserved `#---` fragment returns YAML front matter only when the first
logical line after an optional BOM is exactly `---` and a later logical line is
exactly `---` or `...`. Both delimiter lines are included. Later horizontal
rules and delimiter lines with extra characters do not count.

Anchors cannot be combined with `start_line`/`end_line`. A final `#` is
interpreted as a fragment only when the portion before it ends in `.md` or
`.markdown`, case-insensitively, so names such as `draft#notes.txt` and
`draft#notes.md` remain readable. The complete file still passes confinement,
visibility, symlink, binary, UTF-8, and 5 MiB validation before any section is
selected.

Expected selection failures use these stable messages:

- `Markdown anchor is empty`
- `Markdown anchor is malformed`
- `Markdown anchor is ambiguous`
- `Markdown anchor was not found`
- `Markdown front matter was not found`
- `Anchored reads cannot use line ranges`
- `Markdown anchors require .md or .markdown files`

Malformed anchors include incomplete or non-UTF-8 percent escapes and decoded
whitespace, control/format characters, path separators, or `#`.

## Granular line access

`read_text(path)` returns the whole file. Supplying both optional range arguments
as `read_text(path, start_line, end_line)` returns only the selected complete
logical lines, without line-number decoration or newline normalization. The two
range arguments must be supplied together.

`write_lines(path, start_line, end_line, content)` atomically replaces those
complete lines; empty `content` deletes the range, while replacement content
may expand or contract it. Both bounds are one-based and inclusive. Booleans,
non-integers, line 0, reversed ranges, empty ranges, and coordinates beyond the
current file are rejected. Empty files have no addressable lines and must be
initialized with `write_text`.

For example, a compiler diagnostic at line 12 maps to `start_line = 12` and
`end_line = 12`. For a Git hunk
`@@ -old_start,old_count +new_start,new_count @@`, use the `+` side:

```text
start_line = new_start
end_line = new_start + new_count - 1
```

An omitted Git count means one line. A zero-count hunk describes a position
between lines and cannot be passed to the line tools.

Existing `\n` and `\r\n` line endings are returned exactly. Replacement text
uses the selected range's newline convention, then the nearest preceding
convention, then the nearest following convention, and finally `\n` when the
file has none. A replacement through the end of the file preserves whether the
original ended with a newline. UTF-8 BOMs are omitted from read results, as with
whole-file `read_text`, and are preserved by `write_lines`. Unchanged prefix and
suffix bytes are preserved.

Ranged reads and line writes validate the entire source as UTF-8 text even
though `read_text` returns only the selected range. The same extension, binary
signature, NUL, UTF-8, 5 MiB, traversal, symlink, hidden-path, protected-path,
and permission checks apply to whole-file and line-range operations. The
resulting file from `write_lines`, not merely its new content, must fit within
5 MiB. Coordinates refer to the file state at the start of each call, so a
line-count-changing write can make later coordinates stale.

## Run

No packages need to be downloaded:

macOS or Linux:

```sh
python3 /path/to/rooted-files-mcp/server.py /path/to/folder/to/expose
```

Windows PowerShell:

```powershell
py -3 "C:\path\to\rooted-files-mcp\server.py" "C:\path\to\folder\to\expose"
```

The positional root remains the simplest launch form and does not require a
configuration file. Configuration-only startup is also supported:

macOS or Linux:

```sh
python3 /path/to/rooted-files-mcp/server.py --workspace /path/to/workspace
```

Windows PowerShell:

```powershell
py -3 "C:\path\to\rooted-files-mcp\server.py" --workspace "C:\path\to\workspace"
```

## Workspace configuration

The server looks for `.mcp/rooted-files-mcp.ini` inside the workspace. The
workspace is selected by `--workspace` when provided, otherwise by the
positional root, or by the current working directory for configuration-only
startup.

```ini
[paths]
root = .

[permissions]
read = true
write = true

[features]
mode = standard
show_hidden = false
hidden_allowlist =
    .editorconfig
    .github
```

For configuration-only startup, `[paths] root` is required. Relative roots are
resolved from the workspace. An INI-configured root must remain inside the
workspace after resolving traversal and symbolic links; a positional CLI root
is an explicit trusted override and may be elsewhere. Roots containing spaces
and native macOS, Linux, or Windows path syntax are supported. Model-facing
paths remain relative to the effective root and should use forward slashes.

Settings use this precedence: explicit command-line value, INI value, then the
built-in default. With no corresponding INI or CLI setting, mode is `standard`,
read and write are enabled, and hidden paths are visible. Set
`[features] mode = markdown` or use `--mode markdown` for a read-only Markdown
host; `--mode standard` can override an INI value. Boolean command-line
overrides are paired so either value can override the INI:

```text
--read / --no-read
--write / --no-write
--show-hidden / --hide-hidden
--mode standard / --mode markdown
```

In `standard` mode the permission-filtered five-tool behavior is unchanged. In
`markdown` mode, `tools/list` contains only `read_text` when read permission is
enabled and is empty when it is disabled. Only requested and resolved `.md` and
`.markdown` files are accepted, including whole-file, line-range,
heading-section, and front-matter reads. Directory discovery and every write
are unavailable even if write permission is enabled; the same restrictions are
enforced below MCP dispatch.

When `show_hidden = false`, every dot-prefixed path component is hidden on all
platforms. On Windows, the native Hidden attribute also hides files, folders,
and symbolic-link or reparse-point entries. Hidden entries are removed from
`list_dir` and `tree`, do not consume the 100-entry tree limit, and cannot be
read or written directly. The same checks apply to every requested component
and its resolved in-root symbolic-link target.

The built-in hidden allowlist contains `.gitignore` and `.env.template`.
`hidden_allowlist` adds exact, single-component names; it does not replace the
built-ins. Put one name on each indented continuation line. An allowlisted name
may identify a file or folder, but each nested component is checked separately.
Names are limited to 255 characters and 64 configured entries. Empty values,
duplicates, `.`, `..`, path separators, NUL bytes, and protected-name
collisions are rejected at startup.

The root-relative component `.mcp` is always protected, including when
`show_hidden = true`, and can never be allowlisted. This prevents the model from
listing, reading, or changing `.mcp/rooted-files-mcp.ini` after the server loads
it. Direct hidden or protected access returns the stable error `Hidden path
access is denied` without identifying which component caused the denial.

Read permission controls whole-file, ranged, and Markdown-selection
`read_text` calls in both modes. In standard mode, write permission controls
`write_text` and `write_lines`. The removed `line_access` INI setting and
`--line-access`/`--no-line-access` CLI options are rejected as unknown input.

The configuration must be a regular UTF-8 file no larger than 64 KiB. NUL
bytes, malformed or duplicate INI entries, invalid booleans, unknown sections
or keys, inaccessible roots, and configuration or root symlink escapes fail at
startup. Diagnostics go to stderr; stdout remains reserved for JSON-RPC. A
missing configuration file is allowed when a positional root is present. MCP
stdin, stdout, and stderr are always strict UTF-8, independent of the host
console or locale encoding.

Example LM Studio MCP configuration for macOS or Linux:

```json
{
  "mcpServers": {
    "rooted-files": {
      "command": "/absolute/path/to/python3",
      "args": [
        "/path/to/rooted-files-mcp/server.py",
        "/path/to/folder/to/expose"
      ]
    }
  }
}
```

For a Markdown-only host, add the mode argument:

```json
{
  "mcpServers": {
    "markdown-files": {
      "command": "/absolute/path/to/python3",
      "args": [
        "/path/to/rooted-files-mcp/server.py",
        "/path/to/folder/to/expose",
        "--mode",
        "markdown"
      ]
    }
  }
}
```

Windows example:

```json
{
  "mcpServers": {
    "rooted-files": {
      "command": "C:\\Path\\To\\Python\\python.exe",
      "args": [
        "C:\\path\\to\\rooted-files-mcp\\server.py",
        "C:\\path\\to\\folder\\to\\expose"
      ]
    }
  }
}
```

Replace all example paths. On macOS or Linux, locate Python with
`command -v python3`. In Windows PowerShell, use `(Get-Command python).Source`.
LM Studio needs the real interpreter path rather than the `py` launcher used in
interactive PowerShell commands. Backslashes in JSON strings must be doubled.

In LM Studio, open the **Program** tab, choose **Install → Edit mcp.json**, add
the server entry, and save. LM Studio reloads saved MCP servers automatically.

### GUI definition helper

To generate these values without manually locating or escaping paths, launch
the tkinter helper:

macOS or Linux:

```sh
/path/to/rooted-files-mcp/scripts/generate_mcp_definition.sh
```

Windows Command Prompt or PowerShell:

```bat
C:\path\to\rooted-files-mcp\scripts\generate_mcp_definition.cmd
```

The Python installation must satisfy the server's Python 3.10-or-newer
requirement.

Choose the folder that the server may expose, select `standard` or `markdown`,
and click **Generate definition**. The helper shows:

- a complete `mcpServers` JSON object for LM Studio's `mcp.json` and compatible
  JSON-based MCP harnesses; and
- an `[mcp_servers.rooted-files]` TOML table for the ChatGPT Codex app's
  `config.toml`.

Both snippets contain the Python interpreter used to launch the helper plus the
server launcher, served root, `--mode`, and selected mode. Copy the applicable
snippet and merge its server entry into the target host file. The helper only
displays and copies values; it never writes or merges `mcp.json`, `config.toml`,
or another host configuration file.

## Test

```sh
python3 -m unittest discover -s tests -v
```

On Windows, run `py -3 -m unittest discover -s tests -v`. Symbolic-link security
tests are skipped when the account cannot create symbolic links; enable Windows
Developer Mode or run with the required privilege to exercise them.

The runtime uses only the Python standard library. No package download is
needed to run or test from this source tree, so an offline machine only needs a
prepared Python 3.10-or-newer installation and a local copy of the repository.
