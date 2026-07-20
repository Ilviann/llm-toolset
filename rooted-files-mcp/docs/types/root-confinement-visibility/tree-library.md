# Library: bounded tree rendering

**Source:** `RootedFilesystem.tree` in `rooted_files_mcp/filesystem.py`

Renders a stable Unicode tree from a resolved readable folder, using the same filter/order/labels as direct listing. It counts at most 100 visible entries, marks truncation, reports unreadable nested folders without aborting, and never recurses into directory symlinks.

Hidden/pruned entries do not consume the limit. The function returns presentation text, not filesystem objects or absolute paths.
