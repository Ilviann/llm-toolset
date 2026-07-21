# Architecture components

Each file in this directory documents one implemented cohesive component: what it owns, the source files inside it, its dependency direction, its invariants, and the checks to run when it changes.

- [`python-mcp-server.md`](python-mcp-server.md) — stdio protocol, schemas, discovery, authenticated bridge client, errors, and shutdown.
- [`editor-bridge.md`](editor-bridge.md) — plugin composition, credentials, listener/route ownership, dispatch, commands, limits, and heartbeat.
- [`blueprint-inspector.md`](blueprint-inspector.md) — bounded Asset Registry discovery, exact Actor Blueprint inspection, snapshots, identities, values, and cursors.
- [`blueprint-mutator.md`](blueprint-mutator.md) — safe Actor Blueprint creation, compilation, package saving, diagnostics, cleanup, and read-back.
- [`automated-verification.md`](automated-verification.md) — Python, native, public-API-probe, and cross-process verification boundaries.
