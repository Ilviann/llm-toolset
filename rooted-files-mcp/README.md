# Rooted Files MCP

A small, dependency-free MCP server that exposes one folder as `root`. It uses
relative paths, accepts text only, and communicates over stdio.

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
| `read_text` | Read a UTF-8 text file; requires read permission |
| `write_text` | Create or replace a UTF-8 text file; requires write permission |

All paths are relative to the root argument. Absolute paths, `..` escapes, and
symlinks resolving outside root are denied. Known binary/media extensions,
binary signatures, NUL bytes, invalid UTF-8, and files over 5 MiB are denied.
Tools disabled by the effective permissions are omitted from `tools/list` and
direct calls to them are rejected. Hidden-path settings are enforced for every
tool, not only directory output.

The default `tools/list` result, including all four descriptions and input
schemas, is about 840 characters of compact JSON, or roughly 200–300 tokens for
common model tokenizers. Disabling permissions makes the catalog smaller. This
is the tool-schema context cost before system prompts, the conversation, tool
calls, and file contents. Exact usage varies by model and by how the MCP client
represents tool definitions.

When pairing this server with `godot-editor-mcp` for GDScript work, prefer the
Godot server's default `tiny` mode below 8k context. Use `small` only when the
agent also needs Godot asset/import tools; `large` adds the desktop-oriented
selection helper. Expose the closest useful script folder here to keep the
combined schemas and file results bounded.

## Agentic usage by context size

Agentic use is practical when the exposed root and task are kept narrow. The
5 MiB file limit is a safety limit, not a useful context target: `read_text`
returns a whole file, and `write_text` replaces a whole file, so avoid asking a
small-context model to work on files that do not comfortably fit beside its
instructions and tool history.

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
show_hidden = false
hidden_allowlist =
    .editorconfig
    .github
line_access = true
```

For configuration-only startup, `[paths] root` is required. Relative roots are
resolved from the workspace. An INI-configured root must remain inside the
workspace after resolving traversal and symbolic links; a positional CLI root
is an explicit trusted override and may be elsewhere. Roots containing spaces
and native macOS, Linux, or Windows path syntax are supported. Model-facing
paths remain relative to the effective root and should use forward slashes.

Settings use this precedence: explicit command-line value, INI value, then the
built-in default. With no corresponding INI or CLI setting, read and write are
enabled, hidden paths are visible, and line access is enabled. Boolean command
line overrides are paired so either value can override the INI:

```text
--read / --no-read
--write / --no-write
--show-hidden / --hide-hidden
--line-access / --no-line-access
```

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

Line-access configuration is loaded and validated so its precedence is stable,
but the `read_lines` and `write_lines` tools are scheduled for Phase 3 and are
not active yet.

The configuration must be a regular UTF-8 file no larger than 64 KiB. NUL
bytes, malformed or duplicate INI entries, invalid booleans, unknown sections
or keys, inaccessible roots, and configuration or root symlink escapes fail at
startup. Diagnostics go to stderr; stdout remains reserved for JSON-RPC. A
missing configuration file is allowed when a positional root is present.

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

## Test

```sh
python3 -m unittest discover -s tests -v
```

On Windows, run `py -3 -m unittest discover -s tests -v`. Symbolic-link security
tests are skipped when the account cannot create symbolic links; enable Windows
Developer Mode or run with the required privilege to exercise them.
