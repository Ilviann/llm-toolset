## Unreal Editor MCP Guidelines

Use `docs/index.md` as the implementation-knowledge entry point. Use `README.md` for user workflows once it exists, and executable source, package/plugin metadata, behavioral tests, and runtime `capabilities` as the authoritative behavior and compatibility contracts. Keep this `AGENTS.md` limited to durable workflow rules and invariants:

- Before inspecting or changing source, start at `docs/index.md`, read the owning file in `docs/architecture/`, then read the immediate `index.md` and relevant references under `docs/types/`.
- Keep one component per `docs/architecture/*.md` file. Keep custom data types, wire records, collaborator protocols, and reusable function libraries in separate files under a component/module subfolder of `docs/types/`.
- Every directory under `docs/` must contain an `index.md`. Each index describes only its immediate files and subdirectories and links to them with relative paths.
- Update affected architecture/type/library references and their immediate indexes in the same change as source. Update `README.md` only for user-visible operation or setup. Do not create a `CODE.md` monolith.
- Follow `docs/workflow.md` for working-set selection, Python/C++ contract review, verification, documentation, and release consistency.

- Target Unreal Engine 5.7 and newer, with Blueprint game-logic authoring as the primary workflow.
- The first usable release must create Actor Blueprints from a selected valid Actor-derived base class and safely modify existing Actor Blueprints, including component hierarchies, component defaults, variables, and graph logic.
- After Actor Blueprints, prioritize gameplay-framework Blueprint families such as GameMode, GameState, and GameInstance before other specialized Blueprint types.
- Begin with small typed atomic mutations. Prevalidate targets, types, limits, and stale preconditions; use Unreal editor transactions and undo where supported; return bounded compile diagnostics; and keep compilation, saving, and read-back verification observable.
- Later bounded logic-block replacement may update one event handler, function, macro, or selected graph region as a transaction, but must preserve unrelated Blueprint content and build on the atomic mutation primitives.
- Keep the C++ editor bridge localhost-only and authenticate every request with a high-entropy per-project token. Fail closed when credentials cannot be read, generated, durably persisted, or re-read, and never expose the token through discovery or heartbeat data.
- Validate natively on macOS first. Windows support is mandatory, so isolate and test platform-specific paths, editor discovery, plugin loading, and process behavior from the start; preserve Linux portability under the repository-wide policy.
