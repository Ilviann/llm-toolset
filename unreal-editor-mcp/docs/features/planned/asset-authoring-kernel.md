---
feature_id: asset-authoring-kernel
status: planned
depends_on:
  - asset-family-foundation
released_in: null
---

# `asset-authoring-kernel` — Shared asset creation and editing lifecycle

**Outcome:** Asset-family creation and editing reuse one bounded admission, transaction, persistence, read-back, and recovery kernel while retaining typed family semantics.

**Depends on:**

- [`asset-family-foundation`](asset-family-foundation.md)

### Implementation

- Centralize exact targets, writable scope, operation IDs, expected snapshots, stable identities, unsafe-editor-state checks, transaction orchestration, persistence policy, postcondition read-back, cleanup, and rollback.
- Give creation adapters a no-existing-target flow with collision checks and exact failed-creation cleanup. Give edit adapters an exact loaded target only after shared admission succeeds.
- Keep family validation and mutations typed and allowlisted. Preserve existing explicit Blueprint compile/save lifecycle where released contracts require it.
- Migrate reusable lifecycle behavior from Blueprint, Widget, and Game Data authoring without introducing unrestricted reflection or a generic UObject editor.

### Verification and completion gate

- Cover creation collision and cleanup, stale edits, no-ops, transaction restoration, compile/save failure, replay, lost responses, Undo/Redo, restart read-back, and unrelated-content preservation.
- Run complete writable Python, native, headless, build, and packaging suites.
- Complete only when representative existing creation and editing paths use the kernel without wire or behavior changes.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
