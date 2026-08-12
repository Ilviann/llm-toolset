---
feature_id: asset-authoring-kernel
status: completed
depends_on:
  - asset-family-foundation
released_in: "0.41.0"
---

# `asset-authoring-kernel` — Shared asset creation and editing lifecycle

**Outcome:** Asset-family creation and editing reuse one bounded admission, transaction, persistence, read-back, and recovery kernel while retaining typed family semantics.

**Depends on:**

- [`asset-family-foundation`](asset-family-foundation.md)

### Implementation

- Added a transport-neutral native kernel for canonical targets, bounded operation identities, writable scope, collisions, unsafe editor state, transaction orchestration, persistence, registry publication, exact snapshot read-back, failed-creation cleanup, and verified rollback.
- Creation collaborators retain typed asset construction and optional compilation/finalization. Editing collaborators retain allowlisted family validation and mutation; the kernel never exposes unrestricted reflection or generic UObject editing.
- Migrated all published Blueprint-family creation, including Widget Blueprints, plus user-defined-struct/Data Table creation and editing and Widget layout/style property editing without changing their MCP schemas, result envelopes, operation-ledger behavior, or companion API v1.
- Preserved explicit Blueprint compile/save commands. Persistent Game Data rollback now undoes, re-saves, and verifies the prior snapshot; dirty Widget edits use the same transaction and postcondition lifecycle without implicit saving.

### Verification and completion evidence

- `UnrealMCP.AssetAuthoring.KernelLifecycle` covers collision refusal, exact failed-creation cleanup, stale admission, no-op cancellation, failed-postcondition restoration, successful commit, and Undo/Redo.
- Existing Phase 3, Phase 17, Widget-tree, and UMG Automation cases retain compile/save failure, creation retry, identity, stale edit, persistence, unrelated-content, and read-back coverage through the migrated paths.
- UE 5.8 adaptive and true forced-unity Windows editor-target builds, all native Automation, the Python suite, headless integration, and base Win64 packaging are the 0.41.0 release gates. macOS native verification remains preferred follow-up work.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
