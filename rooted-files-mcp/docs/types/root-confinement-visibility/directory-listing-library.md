# Library: directory listing

**Source:** `RootedFilesystem.list_dir` and helpers in `rooted_files_mcp/filesystem.py`

Resolves a readable folder, filters entries through `HiddenPathPolicy.allows_entry`, orders directories before files/symlinks case-insensitively for stable presentation, and labels directories with `/` and symlinks with `@`. Empty results return `(empty)`; inaccessible enumeration returns a bounded safe error.

Filtering happens before presentation so denied names are not disclosed.
