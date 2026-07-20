# Library: input event codec

**Source:** `plugin/addons/godot_mcp/input_event_codec.gd`

Decodes bounded model values into supported Godot `InputEvent` objects and normalizes existing events for exact comparison/responses. Unknown event kinds, keys, axes/buttons, modifiers, devices, or out-of-range values fail. Transactional Input Map mutation remains in its owning project-workflow component.
