# Configuration-policy contracts

Use the index to retrieve only the contract section relevant to the task.

## Type: `Settings`

**Source:** `rooted_files_mcp/configuration.py`

Frozen dataclass passed to the server and filesystem as the complete effective policy.

| Field | Meaning |
| --- | --- |
| `workspace` | Resolved folder containing the fixed optional configuration. |
| `root` | Resolved folder exposed as model-relative `root`. |
| `read`, `write` | Effective permissions after precedence merging. |
| `show_hidden` | Whether non-protected hidden entries may be visible. |
| `hidden_allowlist` | Effective built-in plus configured exact component names. |
| `case_sensitive` | Detected native name-comparison behavior for the root. |
| `mode` | Effective `standard` or read-only `markdown` host policy. |

`Settings.for_root()` supplies backward-compatible trusted-root defaults. Instances are immutable so catalog and filesystem policy cannot drift after startup.

## Type: `IniSettings`

**Source:** `rooted_files_mcp/configuration.py`

Frozen intermediate dataclass containing only validated values explicitly present in `.mcp/rooted-files-mcp.ini`. Each field is optional so CLI/INI/default precedence can distinguish absence from `false` or an omitted host mode.

The optional root is already absolute, resolved, a directory, and confined to the workspace. The hidden allowlist remains an ordered tuple until native-case duplicate checks merge it with built-ins.

## Type: `ConfigurationError`

**Source:** `rooted_files_mcp/configuration.py`

Expected startup failure with a concise message suitable for argparse/stderr. It covers inaccessible or invalid workspace/root/configuration, schema/boolean/allowlist problems, encoding/size bounds, and missing required configuration.

Messages may describe configuration categories but must not leak secrets. Filesystem construction converts this type to `FileAccessError` only for legacy direct-root compatibility.

## Library: configuration file loading

**Source:** `rooted_files_mcp/configuration.py`

`_read_config_file` resolves only `.mcp/rooted-files-mcp.ini` below the workspace, requires a regular file, bounds metadata and read bytes to 64 KiB, rejects NUL, and decodes UTF-8 with optional BOM. `load_ini` parses with interpolation disabled and strict duplicate handling, then `_validate_schema` permits only `[paths] root`, `[permissions] read/write`, and `[features] mode/show_hidden/hidden_allowlist`. Mode accepts only `standard` or `markdown`.

Unknown/default/duplicate/malformed input fails closed. Production code must never accept an alternate model-selected configuration path.

## Library: settings resolution and precedence

**Source:** `rooted_files_mcp/configuration.py`

`load_settings` selects workspace from explicit workspace, positional root, or current directory; loads the fixed INI; resolves the effective root; detects case behavior; and merges each boolean as CLI value, then INI value, then default `true`. Host mode uses CLI, then INI, then `standard`.

An explicit positional root is trusted and may be outside an explicitly selected workspace. An INI root must remain within workspace. Configuration-only startup requires `[paths] root`; legacy positional-root startup may omit the file.

## Library: filesystem case sensitivity

**Source:** `_filesystem_is_case_sensitive` in `rooted_files_mcp/configuration.py`

Detects native name matching without creating a probe file. Windows returns case-insensitive; other systems compare case-swapped existing path components when possible and fall back to platform behavior (macOS insensitive, other POSIX sensitive).

The result controls protected-name and allowlist duplicate comparisons plus case-insensitive actual-entry lookup. Tests must inject/cover Windows, macOS, and POSIX behavior independent of the host.

## Library: hidden allowlist

**Source:** `rooted_files_mcp/configuration.py`

`_hidden_allowlist` parses non-empty continuation lines, caps configured entries at 64 and names at 255 characters, and rejects duplicates, `.`, `..`, separators, NUL, and protected names. `_effective_hidden_allowlist` merges additions with `.gitignore` and `.env.template` and repeats duplicate detection using native case behavior.

Allowlist entries are exact single path components and additive. They never override `.mcp` protection or Windows Hidden-attribute policy for differently named entries.
