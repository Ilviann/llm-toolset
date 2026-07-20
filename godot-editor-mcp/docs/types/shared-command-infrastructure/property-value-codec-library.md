# Library: property value codec

**Source:** `plugin/addons/godot_mcp/property_value_codec.gd`

`convert` decodes the model-facing property value type for a target Godot property; `encode` converts a Godot Variant into bounded JSON/tagged output; `supported_forms` feeds capabilities. Helpers validate finite numbers, depth, elements, keys, strings, packed arrays, encoded bytes, Variant/property hints, resource/node classes, and scene confinement. Transaction-local node handles are available only through an injected resolver.
