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
| `list_dir` | List direct entries in a folder |
| `tree` | Show a recursive tree, limited to 100 entries |
| `read_text` | Read a UTF-8 text file |
| `write_text` | Create or replace a UTF-8 text file |

All paths are relative to the root argument. Absolute paths, `..` escapes, and
symlinks resolving outside root are denied. Known binary/media extensions,
binary signatures, NUL bytes, invalid UTF-8, and files over 5 MiB are denied.

The complete `tools/list` result, including all four descriptions and input
schemas, is about 840 characters of compact JSON, or roughly 200–300 tokens for
common model tokenizers. This is the fixed context cost before system prompts,
the conversation, tool calls, and file contents. Exact usage varies by model and
by how the MCP client represents tool definitions.

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
