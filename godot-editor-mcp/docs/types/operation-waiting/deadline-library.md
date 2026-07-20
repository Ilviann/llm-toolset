# Library: monotonic deadlines

**Source:** private `_Deadline` in `godot_editor_mcp/waiting.py`

Immutable helper holding one absolute monotonic end time. It reports remaining time and expiration so polling, reconnect, diagnostic settling, and sleep intervals consume the same caller-provided timeout.

Never reset the deadline after progress or reconnect. Wall-clock time is unsuitable because system clock changes could extend or prematurely end waits. Tests inject deterministic clocks rather than sleeping.
