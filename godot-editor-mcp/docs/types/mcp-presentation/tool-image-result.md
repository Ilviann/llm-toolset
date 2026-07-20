# Type: `ToolImageResult`

**Source:** `godot_editor_mcp/stdio.py`

Immutable Python value used to distinguish validated binary image output from ordinary JSON/text results.

| Field | Meaning |
| --- | --- |
| `data` | Complete PNG bytes already confined and validated by dispatch. |
| `mime_type` | MIME label; currently expected to be `image/png`. |
| `metadata` | Concise JSON object returned as adjacent text content. |

Only the capture dispatch path constructs this type. Stdio base64-encodes `data`; it does not repeat path, signature, dimension, or size validation. Keep the type internal to the trusted dispatcher/transport boundary.
