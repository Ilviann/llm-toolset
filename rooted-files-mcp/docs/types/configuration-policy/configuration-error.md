# Type: `ConfigurationError`

**Source:** `rooted_files_mcp/configuration.py`

Expected startup failure with a concise message suitable for argparse/stderr. It covers inaccessible or invalid workspace/root/configuration, schema/boolean/allowlist problems, encoding/size bounds, and missing required configuration.

Messages may describe configuration categories but must not leak secrets. Filesystem construction converts this type to `FileAccessError` only for legacy direct-root compatibility.
