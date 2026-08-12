---
feature_id: gameplay-tag-properties
status: planned
depends_on:
  - native-wire-contracts
  - asset-authoring-kernel
released_in: null
---

# `gameplay-tag-properties` — Gameplay Tag property values

**Outcome:** Agents can read and set existing asset properties whose exact reflected type is `FGameplayTag` or `FGameplayTagContainer` through Unreal MCP's supported property workflows.

**Depends on:**

- [`native-wire-contracts`](../completed/native-wire-contracts.md)
- [`asset-authoring-kernel`](../completed/asset-authoring-kernel.md)

### Implementation

- Extend the shared reflected-property codec and Game Data value codec with explicit semantic adapters for `FGameplayTag` and `FGameplayTagContainer`. Reuse them in every existing asset inspection or mutation path backed by those codecs; do not add another model-facing tool or a generic arbitrary-asset editor.
- Encode one tag as its exact tag-name string and one container as a deterministic sorted array of its explicit stored tag names. Use an empty string and empty array for empty values, omit the container's derived parent-tag cache, and bound tag length and container size.
- Accept only exact canonical tag names registered in the live project. Reject unknown, redirected, duplicate, malformed, or over-limit input instead of creating tags, silently canonicalizing names, or partially changing a container. Preserve exact stored names during inspection so legacy invalid state remains visible.
- Keep Gameplay Tag property support in the base plugin as an engine-wide reflected-value concern. Add only the required `GameplayTags` module dependency; do not add `GameplayAbilities` or `GameplayTasks`, require the GAS companion, change the companion API, or duplicate the GAS companion's typed ability/effect sections.
- Preserve the owning operation's writable-mode, snapshot, transaction, mutation-ledger, notification, dirty-state, compile, save, and read-back contracts. Game Data rows may use the adapters recursively through already-supported structs and collections; other property containers and arbitrary structs remain outside this feature.
- Do not edit Gameplay Tag configuration, add or redirect tag definitions, author Gameplay Ability or Gameplay Effect semantics, perform runtime tag queries, or add Blueprint member-type and graph-pin authoring.

### Verification

- Test valid, empty, native, project-defined, and legacy invalid tags; empty and populated containers; deterministic order; explicit-parent versus derived-parent behavior; duplicates; unknown and redirected names; malformed values; and all length, count, request, and response limits.
- Prove exact read/write round trips through representative Blueprint class-default and component-default assets plus direct and nested Data Table row fields. Cover stale snapshots, readonly rejection, replay, rollback, Undo/Redo, save, restart, and unchanged assets after every rejection.
- Run the Python contracts, focused native codec and asset-workflow Automation Tests, full native suite, headless integration, adaptive and forced-unity Windows builds, and base-only packaging without the GAS companion. Record macOS verification as non-blocking follow-up when unavailable.

### Documentation and completion gate

- Document the two canonical value forms, registration and redirect policy, limits, supported asset workflows, exclusions, module-ownership boundary, and read/set examples. Update capabilities, property contracts, dependency documentation, history, and synchronized runtime versions when implemented.
- Complete the feature only when both types persist with exact semantic read-back across every claimed asset workflow, invalid input cannot mutate content, the base remains independent of Gameplay Ability System authoring, and mandatory Windows verification passes.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
