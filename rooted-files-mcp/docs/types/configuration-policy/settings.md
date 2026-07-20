# Type: `Settings`

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

`Settings.for_root()` supplies backward-compatible trusted-root defaults. Instances are immutable so catalog and filesystem policy cannot drift after startup.
