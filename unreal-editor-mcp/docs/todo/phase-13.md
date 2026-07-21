# Phase 13 — Actor workflow hardening on macOS

**Outcome:** The complete create/open → inspect → component/default/member/graph edit → compile → save → inspect workflow is dependable for supervised Actor Blueprint development on macOS.

### Implementation

- Normalize compiler warnings, errors, notes, node and graph references, and source locations into bounded structured diagnostics with bounded continuation where needed.
- Audit every mutation for exact prevalidation, operation reconciliation, transaction scope, postconditions, package dirty state, rollback behavior, and deterministic handling while the editor is compiling, saving, loading, undoing, garbage collecting, playing, or shutting down.
- Measure tool-schema and representative-result token costs. Tighten descriptions, defaults, filters, result pages, and modes for small local models without hiding required identity, safety, or operation fields.
- Add deterministic seeded schema/value fuzz cases without third-party runtime dependencies.
- Maintain the canonical end-to-end fixture builder and behavioral integration suite in the disposable test project. Expected results must come from runtime contracts and created assets, not prose files or committed generated assets.

### Verification

- Run repeated macOS sessions against clean and already-edited Actor Blueprints, including malformed requests, stale snapshots, lost responses, cancellation, timeout, compiler failure, save conflict, editor restart, bridge restart, and undo/redo.
- Run bounded native JSON/property decoder tests, operation-ledger stress tests, complete Python tests, Unreal Automation Tests, and cross-process workflows.
- Record peak request/response bytes, retained state, startup time, schema size, and typical operation latency on the 16 GB macOS development machine.

### Documentation and completion gate

- Complete the Actor-focused README, troubleshooting, security model, limits, mode guidance, LM Studio setup, operation recovery, macOS setup, and end-to-end tutorial.
- Complete the phase only when the documented clean-project and existing-Blueprint workflows pass repeatedly on native macOS.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
