# Library: process entry and composition

**Sources:** `server.py`, `rooted_files_mcp/__main__.py`, `rooted_files_mcp/server.py`

The root script and package module delegate to `main()`. `main()` defines the positional root, workspace, read/write, and hidden-visibility CLI; resolves immutable settings; and converts startup configuration/filesystem errors into argparse diagnostics. `run(settings)` accepts effective `Settings` or a legacy root, constructs `RootedFilesystem` and `MCPServer`, then owns the stdio loop.

Keep entry points behaviorally identical. New CLI policy belongs in configuration types first, then composition and README examples.
