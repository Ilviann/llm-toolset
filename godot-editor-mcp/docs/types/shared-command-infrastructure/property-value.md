# Type: property value contract

**Source:** `plugin/addons/godot_mcp/property_value_codec.gd`

Model-facing representation of bounded Godot Variant values. Ordinary JSON scalars, arrays, and dictionaries remain direct. Non-JSON or reference values use explicit `$type` dictionaries for node/resource/NodePath, rectangles/transforms/quaternion/plane/AABB/basis, enum/flags, and packed arrays. Concise numeric arrays remain supported for common vectors/colors.

Validation and conversion operations are documented separately in [`property-value-codec-library.md`](property-value-codec-library.md).
