# Type: input event contract

**Source:** `plugin/addons/godot_mcp/input_event_codec.gd`

Normalized dictionaries represent the supported Input Map events:

- key: logical/numeric key, optional physical mode and modifiers;
- mouse button: fixed name/number and device;
- joypad button: fixed name/number and device;
- joypad motion: fixed axis, direction `-1|1`, and device.

Decoding and normalization operations are documented separately in [`input-event-codec-library.md`](input-event-codec-library.md).
