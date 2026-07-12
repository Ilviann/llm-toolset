# Rooted Files MCP

A small, dependency-free MCP server that exposes one folder as `root`. It uses
relative paths, accepts text only, and communicates over stdio.

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

```sh
python3 /path/to/rooted-files-mcp/server.py /path/to/folder/to/expose
```

Example LM Studio MCP configuration:

```json
{
  "mcpServers": {
    "rooted-files": {
      "command": "/opt/homebrew/bin/python3",
      "args": [
        "/path/to/rooted-files-mcp/server.py",
        "/path/to/folder/to/expose"
      ]
    }
  }
}
```

Replace both example paths. The shown Python command is the one detected on the
current Mac. If it changes, run `command -v python3` and use that full path.

In LM Studio, open the **Program** tab, choose **Install → Edit mcp.json**, add
the server entry, and save. LM Studio reloads saved MCP servers automatically.

## Test

```sh
python3 -m unittest discover -s tests -v
```
