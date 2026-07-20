# Types: autoload record and patch

**Source:** `project_workflow_commands.gd`; schema in `tool_catalog.py`

Public records contain name, normalized `res://` path, singleton flag, compare-and-swap `value`, and protected status. A patch contains bounded `add`, `update`, or `remove` changes; add/update require an existing Node script or PackedScene path, and every change may state the expected prior value.

The complete virtual result is validated before guarded `EditorPlugin` APIs apply it. Invalid/conflicting/protected names or paths, stale expectations, application mismatch, or save failure reject/roll back the entire batch. The reserved runtime probe remains immutable through this type.
