# Architecture components

Each file documents one cohesive component: what it owns, the source files inside it, its dependency direction, its invariants, and the checks to run when it changes.

- [`python-entry-composition.md`](python-entry-composition.md) — Python process entry points and concrete service composition.
- [`mcp-presentation.md`](mcp-presentation.md) — JSON-RPC/MCP request handling and stdio presentation.
- [`tool-api-dispatch.md`](tool-api-dispatch.md) — tool catalog, schema policy, routing, and dispatch.
- [`local-project-process-services.md`](local-project-process-services.md) — confined asset writes and optional editor launch.
- [`bridge-discovery-errors.md`](bridge-discovery-errors.md) — authenticated bridge client, discovery, and shared domain errors.
- [`operation-waiting.md`](operation-waiting.md) — validated state views and bounded asynchronous waits.
- [`plugin-lifecycle-bridge-routing.md`](plugin-lifecycle-bridge-routing.md) — editor plugin composition, authenticated startup, network bridge, discovery, and routing.
- [`shared-command-infrastructure.md`](shared-command-infrastructure.md) — Godot-side guards, codecs, limits, records, errors, cursors, operations, and events.
- [`editor-state-diagnostics.md`](editor-state-diagnostics.md) — scene/run/import/project-file state and diagnostics.
- [`asset-scene-services.md`](asset-scene-services.md) — asset operations, edited/runtime inspection routing, scene creation, and atomic transactions.
- [`project-workflows-reload.md`](project-workflows-reload.md) — settings, Input Map, autoload/plugin metadata, and project reload.
- [`runtime-debugger-validation.md`](runtime-debugger-validation.md) — debugger-only runtime inspection, capture, input, and condition validation.
- [`automated-verification.md`](automated-verification.md) — Python, headless Godot, and cross-process verification.
