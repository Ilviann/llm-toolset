---
feature_id: asset-delete
status: completed
depends_on:
  - asset-references
released_in: "0.20.0"
---

# `asset-delete` — Delete asset

**Implementation status:** Completed in 0.20.0 and verified natively on macOS and Windows.

**Outcome:** Agents can delete one exact, unreferenced asset package through Unreal Editor only after conservative reference and editor-state preflight, with a retained and verified result.

**Depends on:**

- [`asset-references`](asset-references.md)

### Implementation

- Add a ledger-backed `asset_delete` tool accepting a caller-generated `operation_id`, one exact asset object path, and the current asset/reference snapshot. Never accept a filesystem path or a force-delete option.
- Confine deletion to `/Game` and symlink-free content mounts owned by plugins physically inside the current project's `Plugins/` directory. Reject engine, external, marketplace, transient, generated, redirector, and otherwise read-only targets.
- Require a single-asset package and refuse current maps, dirty packages, active PIE/simulation, saving, compilation, async loading, garbage collection, undo/redo, conflicting retained work, or any state whose safety cannot be proven.
- Re-run bounded serialized and live-memory reference preflight immediately before mutation. Require complete registry categories; reject any referencer, unsupported or stale scan, truncated registry scan, stale snapshot, or newly observed reference. A truncated live diagnostic requires Unreal's full deletion-specific memory/Undo reference check to prove no retained reference.
- Reject an open editor for the exact target without closing it. Release benign tool-owned references and use package unloading only when the selected public deletion path requires it; unloading must not be treated as proof that serialized references are absent.
- Delete through a compiled, supported Unreal Editor asset/package API on the Game thread. Do not call the force-delete editor subsystem path, delete `.uasset` files directly, fix redirectors implicitly, rewrite referencers, clear unrelated transactions, or claim Undo support that Unreal cannot provide.
- Reconcile lost responses through `operation_status`. Verify the object and package are absent from the Asset Registry and storage view before reporting `committed`; return explicit partial or unknown outcomes if the editor API and persistence state disagree.
- Publish deletion, reference, memory, package, editor-state, operation-retention, timeout, and verification capabilities and stable refusal errors.

### Verification

- Test clean unreferenced assets across supported project mounts and reject hard, soft, management/searchable-name, live-memory, open-editor, map/world, dirty, multi-asset-package, redirector, generated, read-only, engine, external-plugin, stale, truncated, and unsupported targets.
- Test editor-close refusal, unload refusal, source-control read-only behavior, API failure, persistence/registry disagreement, timeout, replay, lost response, operation-ID conflict, restart read-back, and concurrent reference creation.
- Prove rejection preserves files, packages, dirty state, editors, selection, transactions, and referencers. Prove success removes only the exact package and does not modify dependent content or neighboring assets.
- Run Python schema tests, focused native Automation, the full affected suite, and production-bridge cross-process deletion tests on macOS and Windows while preserving Linux source portability.

### Documentation and completion gate

- Document scope, preflight, snapshot and operation requirements, non-Undo semantics, editor closing/unloading behavior, source-control effects, verification, recovery guidance, and why direct or force deletion is excluded.
- Add a bounded example that inspects references, deletes one clean disposable asset, reconciles the operation, and verifies absence after restart.
- Complete the feature only when an agent can delete a disposable unreferenced project asset without bypassing Unreal, damaging referencers, silently discarding dirty work, or reporting unverifiable success.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
