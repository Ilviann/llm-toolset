# Type: Input Map patch

**Source:** `input_map_commands.gd`; schema in `tool_catalog.py`

Request identifies one action, optional deadzone, normalized events to add/remove, and save behavior. Event values use the shared input-event contract. The operation preserves unrelated events, treats normalized exact duplicates deterministically, updates live `InputMap`, and saves through `ProjectSettings` transactionally.

The result reports normalized action state/diff and refresh/reload requirements. Validation or save failure leaves the original action configuration in place.
