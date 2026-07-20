# Library: atomic no-replace publication

**Source:** `godot_editor_mcp/assets.py`

The private publication helpers turn a fully written, flushed, same-directory temporary file into a destination without replacement:

- POSIX uses a same-filesystem hard link so publication fails if the destination exists.
- Windows uses native rename semantics configured to fail on an existing destination.
- The containing directory is synchronized where supported and temporary cleanup is deterministic.

Do not replace this with a check-then-rename sequence: another writer can win between validation and publication. Preserve the deterministic race and mocked Windows tests.
