# Configuration-policy types and libraries

- [Type: `Settings`](contracts.md#type-settings) — immutable effective runtime policy.
- [Type: `IniSettings`](contracts.md#type-inisettings) — validated optional INI values before precedence merging.
- [Type: `ConfigurationError`](contracts.md#type-configurationerror) — safe startup failure type.
- [Library: configuration file loading](contracts.md#library-configuration-file-loading) — fixed-path bounded INI reading and closed-schema parsing.
- [Library: settings resolution and precedence](contracts.md#library-settings-resolution-and-precedence) — workspace/root resolution and CLI/INI/default precedence.
- [Library: filesystem case sensitivity](contracts.md#library-filesystem-case-sensitivity) — non-writing native case behavior detection.
- [Library: hidden allowlist](contracts.md#library-hidden-allowlist) — configured-name validation and effective allowlist merging.
