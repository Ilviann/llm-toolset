# Library: hidden allowlist

**Source:** `rooted_files_mcp/configuration.py`

`_hidden_allowlist` parses non-empty continuation lines, caps configured entries at 64 and names at 255 characters, and rejects duplicates, `.`, `..`, separators, NUL, and protected names. `_effective_hidden_allowlist` merges additions with `.gitignore` and `.env.template` and repeats duplicate detection using native case behavior.

Allowlist entries are exact single path components and additive. They never override `.mcp` protection or Windows Hidden-attribute policy for differently named entries.
