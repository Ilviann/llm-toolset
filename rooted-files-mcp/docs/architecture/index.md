# Architecture components

Each file documents one cohesive component: ownership, source files, dependencies, invariants, known pressure, and checks to run when it changes.

- [`process-entry-composition.md`](process-entry-composition.md) — script/module/installed entry points, CLI parsing, and composition.
- [`configuration-policy.md`](configuration-policy.md) — workspace INI validation, precedence, roots, permissions, and immutable effective settings.
- [`root-confinement-visibility.md`](root-confinement-visibility.md) — root resolution, permissions, hidden/protected paths, directory listing, and bounded trees.
- [`text-validation-atomic-editing.md`](text-validation-atomic-editing.md) — text classification, UTF-8/range reads, format preservation, and atomic writes.
- [`mcp-api-stdio.md`](mcp-api-stdio.md) — compact tool catalog, JSON-RPC/MCP dispatch, and protocol-safe stdio.
- [`automated-verification.md`](automated-verification.md) — configuration, filesystem security, protocol, and subprocess verification.
