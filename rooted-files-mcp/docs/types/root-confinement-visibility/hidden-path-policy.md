# Type: `HiddenPathPolicy`

**Source:** `rooted_files_mcp/filesystem.py`

Stateful policy constructed from effective root, visibility, allowlist, case behavior, and optional injected Windows/stat behavior. It exposes:

- `check_names` for unresolved requested components;
- `check` for both requested and resolved in-root components;
- `allows_entry` for non-disclosing listing filters;
- `has_windows_hidden_attribute` as an independently testable platform branch.

On case-insensitive roots it discovers the actual directory entry name before evaluating protection/allowlisting. `.mcp` always fails. A visible symlink alias cannot bypass a hidden target check.
