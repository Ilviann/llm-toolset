# Root-confinement/visibility contracts

Use the index to retrieve only the contract section relevant to the task.

## Types: filesystem access errors

**Source:** `rooted_files_mcp/filesystem.py`

`FileAccessError` is the public expected-failure boundary returned as an MCP tool error. It covers permissions, invalid/outside/missing paths, hidden access, folder/file mismatch, text classification, range coordinates, size/encoding, and safe OS-operation messages.

Private `_HiddenPathError` marks a hidden/protected denial so listings can prune entries without disclosing which component was denied. Direct access exposes only the stable message `Hidden path access is denied`.

## Type: `HiddenPathPolicy`

**Source:** `rooted_files_mcp/filesystem.py`

Stateful policy constructed from effective root, visibility, allowlist, case behavior, and optional injected Windows/stat behavior. It exposes:

- `check_names` for unresolved requested components;
- `check` for both requested and resolved in-root components;
- `allows_entry` for non-disclosing listing filters;
- `has_windows_hidden_attribute` as an independently testable platform branch.

On case-insensitive roots it discovers the actual directory entry name before evaluating protection/allowlisting. `.mcp` always fails. A visible symlink alias cannot bypass a hidden target check.

## Library: runtime permissions

**Source:** `RootedFilesystem._require_read` and `_require_write` in `rooted_files_mcp/filesystem.py`

Every public operation invokes its owning permission helper before resolving or touching a model path. Read covers listing, tree, and whole/ranged reads; write covers full and ranged writes.

Host-mode gates complement permissions: Markdown mode permits only supported
Markdown paths through `read_text` and rejects listing, tree, and writes. These
checks intentionally duplicate MCP catalog filtering. Keep both layers
synchronized so direct service calls or stale/crafted tool calls cannot bypass
effective policy.

## Library: rooted path resolution

**Source:** `RootedFilesystem.resolve` in `rooted_files_mcp/filesystem.py`

Validates a string, NUL-free, non-absolute model path; normalizes requested components; resolves it strictly or non-strictly according to operation policy; proves the result remains under root; and applies hidden policy to both requested and resolved paths. If resolution fails, name-only policy runs before the generic outside/missing error so protected/hidden direct access remains stable.

All public filesystem paths must pass through this function. Do not add alternate `Path` joining/resolution paths in text or listing code.

## Library: directory listing

**Source:** `RootedFilesystem.list_dir` and helpers in `rooted_files_mcp/filesystem.py`

Resolves a readable folder, filters entries through `HiddenPathPolicy.allows_entry`, orders directories before files/symlinks case-insensitively for stable presentation, and labels directories with `/` and symlinks with `@`. Empty results return `(empty)`; inaccessible enumeration returns a bounded safe error.

Filtering happens before presentation so denied names are not disclosed.

## Library: bounded tree rendering

**Source:** `RootedFilesystem.tree` in `rooted_files_mcp/filesystem.py`

Renders a stable Unicode tree from a resolved readable folder, using the same filter/order/labels as direct listing. It counts at most 100 visible entries, marks truncation, reports unreadable nested folders without aborting, and never recurses into directory symlinks.

Hidden/pruned entries do not consume the limit. The function returns presentation text, not filesystem objects or absolute paths.
