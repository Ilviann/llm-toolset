# Library: error envelopes

**Source:** `plugin/addons/godot_mcp/error_envelope.gd`

Centralizes mirrored error-code constants, constructs success/failure dictionaries, bounds untrusted detail depth/size, classifies legacy string failures, and extracts safe public messages. All editor command services use these helpers instead of inventing response shapes.
