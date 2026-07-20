# Type: project settings patch

**Source:** `project_settings_commands.gd`; schema in `tool_catalog.py`

Request contains up to the published limit of changes, plus `save` and `dry_run`. Each change names one permitted key, provides a bounded property value, and may include `expected` for compare-and-swap protection.

The full batch is normalized and validated before mutation. Results report the normalized diff and required refresh/reload level. A stale expected value rejects everything; failed save restores all original values. Secret/internal keys and general Input Map keys are excluded from this type.
