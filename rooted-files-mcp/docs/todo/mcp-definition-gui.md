# `mcp-definition-gui` — Local MCP definition generator

**Status:** Completed in 0.5.0; native macOS verification passed, with native
Linux and Windows verification tracked in the roadmap backlog.

**Depends on:** `markdown-read`

**Outcome:** An operator can select the folder exposed by Rooted Files MCP,
choose `standard` or `markdown` mode, and copy valid launch configuration into
LM Studio, another JSON-compatible MCP host, or the ChatGPT Codex app without
manually locating or escaping paths.

### Helper contract

- Provide a directly runnable Python script under `scripts/` using tkinter and
  no third-party dependencies.
- Provide an adjacent POSIX shell launcher that resolves its directory and
  invokes the Python helper with `python3`.
- Provide an adjacent Windows CMD launcher that resolves its directory, invokes
  the Python helper with `python`, and preserves its exit code.
- Require an existing served-root folder and offer exactly `standard` and
  `markdown` choices.
- Resolve the interpreter used to run the helper and the repository's direct
  `server.py` launcher.
- Generate a complete `mcpServers` JSON object containing a stable
  `rooted-files` entry, absolute command and argument paths, and an explicit
  `--mode` value.
- Display the server name, Python command, and every ordered argument in
  separate copyable fields for Codex's STDIO server form.
- Copy only on explicit user action and never write, merge, or replace host
  configuration files.
- Fail safely with a local GUI diagnostic when the selected root, interpreter,
  or server launcher is inaccessible or has the wrong type.

### Verification

- Unit-test exact command and argument ordering for both host modes.
- Parse the formatted JSON and verify its complete object structure.
- Cover unknown mode, missing or non-directory root, and invalid launcher
  files.
- Compile/import the helper and perform a native tkinter launch smoke test on
  macOS.
- Run the complete application suite; track unavailable native Linux and
  Windows GUI verification separately.

### Documentation and completion gate

- Document how to launch and use the helper, what each output targets, and that
  it never edits host configuration.
- Update process-entry ownership, its type index, verification ownership,
  README examples, roadmap state, history, and synchronized minor version.
- Complete only when both modes produce launchable definitions, all generated
  values are copyable, automated tests pass, and the GUI opens on the primary
  macOS development host.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
