# Library: atomic JSON records

**Source:** `plugin/addons/godot_mcp/atomic_json_record.gd`

Reads a bounded JSON object and atomically replaces it using a same-directory temporary file. Writes serialize compact JSON, flush/close, rename into place, and clean the temporary path on failure. Reads reject missing/oversized/malformed/non-object data with controlled results.

Discovery and reload are the current consumers. Their record schemas remain owned by those components; this library owns only bounded durable file mechanics. Do not use prose files or arbitrary model paths as record input.
