---
feature_id: level-management
status: completed
depends_on:
  - level-open
  - asset-delete
released_in: "0.25.0"
---

# `level-management` — Level management

**Implementation status:** Completed in 0.25.0 and verified natively on Windows. macOS verification remains in the native platform backlog.

**Outcome:** Agents can create, perform bounded initial setup on, save, and safely delete exact Unreal map assets without raw `.umap` filesystem access, implicit dirty-work decisions, or orphaned map-owned packages.

**Depends on:**

- [`level-open`](level-open.md)
- [`asset-delete`](asset-delete.md)

### Implementation

- Add a ledger-backed `level_manage` tool with exact `create` and `configure` operations. Address source, destination, and current maps only by mounted `UWorld` asset paths; never accept a `.umap` filename or arbitrary filesystem path.
- Create one map at a new writable `/Game` or symlink-free local-project-plugin path from either an Unreal-provided blank world or one exact clean mounted template. Never overwrite a package, mutate the template, or infer a template from editor preferences.
- Make any current-map switch explicit in the request and reuse `level_open` safety rules. Reject dirty work, PIE/simulation, saving, compilation, async loading, garbage collection, undo/redo, conflicting operations, and any prompt-requiring state rather than saving or discarding implicitly.
- Define setup as a bounded allowlist of live World Settings and map-creation options, including a compatible GameMode override and other safely reflected scalar, enum, struct, or asset/class-reference values proven by the shared property codec. Return exact changed-property read-back.
- Treat World Partition, One File Per Actor/external-actor layout, streaming topology, and related package structure as explicit creation/template facts. Report their effective state and reject unsupported post-creation conversion instead of silently running commandlets or changing project settings.
- Save and verify the new or configured map and every explicitly affected map-owned package before reporting commit. Return exact map identity, revision, snapshot, effective setup, and per-package persistence results; report partial persistence honestly.
- Extend the `asset_delete` path for maps rather than adding force or filesystem deletion to `level_manage`. Require the target not be current, loaded as a sublevel or streaming level, dirty, or otherwise active.
- Before deletion, enumerate the complete bounded map-owned package closure, including World Partition external actor/object packages and separate build-data packages where applicable. Re-run reference preflight for the whole closure and reject external referencers, live-memory referencers, truncation, unsupported ownership, stale snapshots, or unsafe editor state.
- Delete the verified closure only through supported Unreal Editor asset/package APIs. Never directly remove `.umap`, external-actor, build-data, or sidecar files; never rewrite referencers, fix redirectors implicitly, or claim filesystem atomicity or Undo support.
- Reconcile every create, configure, save, and delete outcome through `operation_status`. Verify creation/configuration after reload and deletion through both Asset Registry and storage views before reporting a terminal committed result.
- Publish template, setup-property, World Partition, package-closure, operation, timeout, save, deletion, and verification capabilities and stable errors.

### Verification

- Test blank and exact-template creation, duplicate destinations, supported and unsupported World Settings, compatible and incompatible GameMode overrides, explicit opening, clean-current-map preservation, save/reload read-back, operation replay, and stale snapshots.
- Test non-World-Partition and World Partition maps, external actors/objects, streaming and sublevel use, build-data packages, template package dependencies, local project-plugin mounts, read-only mounts, and every package-closure bound.
- Reject current, dirty, referenced, live-memory-referenced, loaded-sublevel, engine, external-plugin, truncated, stale, partially owned, and unsafe map deletion targets without changing any package or editor state.
- Test source-control read-only state, editor close/unload refusal, save and deletion API failures, partial package persistence/removal, registry/storage disagreement, timeout, lost response, restart reconciliation, and recovery diagnostics.
- Prove creation never mutates its template, setup changes only requested allowlisted fields, rejection never saves or discards dirty work, and successful deletion leaves no owned external packages while preserving unrelated content.
- Run Python schema tests, compiled Unreal 5.8 public-API probes, focused native Automation, the full affected suite, and production-bridge cross-process create/setup/delete workflows on macOS and Windows while preserving Linux source portability.

### Documentation and completion gate

- Document mounted map paths versus `.umap` files, blank/template creation, setup scope, World Partition constraints, explicit map switching, snapshots, package ownership, save/delete verification, partial failure, non-Undo semantics, and recovery.
- Add one bounded example that creates a map, applies and verifies initial World Settings, saves and reloads it, opens another clean map, confirms the created map is unreferenced, deletes its complete owned package set, and verifies absence after restart.
- Complete the feature only when an agent can create, initialize, persist, and safely remove representative non-World-Partition and World Partition map assets without raw file access, orphaned packages, implicit dirty-work decisions, or unverifiable success.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
