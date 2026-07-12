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
