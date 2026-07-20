# Library: atomic replacement

**Source:** `_write_target` and `_replace_atomically` in `rooted_files_mcp/filesystem.py`

Initial validation resolves the model path, rejects binary names, confines the real parent, and validates an existing target as text even in write-only mode. Publication writes a same-directory `.rooted-mcp-*` temporary file, flushes and `fsync`s it, copies the existing mode when applicable, then re-resolves the original model path and revalidates path, parent, existence, hidden policy, and existing text immediately before `os.replace`.

Path/parent changes or a disappeared required target fail. OS and policy failures remove the temporary file. This repeated validation is deliberate TOCTOU defense and must survive refactoring.
