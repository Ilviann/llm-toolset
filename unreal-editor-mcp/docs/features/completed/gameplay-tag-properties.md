---
feature_id: gameplay-tag-properties
status: completed
depends_on:
  - native-wire-contracts
  - asset-authoring-kernel
released_in: "0.47.0"
---

# `gameplay-tag-properties` — Gameplay Tag property values

**Outcome:** Agents can read and set existing asset properties whose exact reflected type is `FGameplayTag` or `FGameplayTagContainer` through Unreal MCP's supported property workflows.

**Depends on:**

- [`native-wire-contracts`](native-wire-contracts.md)
- [`asset-authoring-kernel`](asset-authoring-kernel.md)

## Delivered contract

- The shared reflected-property and Game Data codecs use one exact-type semantic adapter. `FGameplayTag` is one exact tag-name string; `FGameplayTagContainer` is a case-sensitive sorted array of explicit stored names. Empty values use `""` and `[]`, and derived parent caches are omitted.
- Writes accept only exact canonical names registered in the live project. Unknown, redirected, duplicate, malformed, over-256-character, and over-64-item input rejects atomically. Inspection preserves exact stored names, including legacy invalid state.
- Blueprint class defaults, Blueprint component defaults, direct and nested Data Table fields, `game_data_inspect`, and structured `asset_inspect` views share the semantic forms. Gameplay Tags are semantic selector leaves rather than reflected internal structs.
- The base plugin depends only on the engine-wide `GameplayTags` module. It does not add `GameplayAbilities` or `GameplayTasks`, require the GAS companion, change companion API v2, edit tag configuration, or add Gameplay Ability, Gameplay Effect, runtime-query, Blueprint member-type, or graph-pin authoring.
- Existing writable-mode, snapshot, transaction, retained-operation, dirty-state, compile, save, and read-back contracts remain authoritative. Rejected same-snapshot edits also restore the package's prior dirty state.

## Verification

- Python: 178 tests passed with one existing skip.
- Native: 62 `UnrealMCP` Automation cases passed, including focused codec, Blueprint default/component, Data Table, structured asset-inspection, rejection, dirty-state, Undo/Redo, compile, and save coverage.
- Cross-process: the production-socket lifecycle passed creation, exact semantic read-back, replay, invalid-write preservation, graceful shutdown, restart, and persisted read-back.
- Windows: adaptive, true forced-unity, and explicit non-unity UE 5.8 Editor builds passed. Base-only Win64 packaging passed without either optional companion.
- macOS follow-up passed on 2026-08-15 through the 67-case native suite, production-socket restart workflow, all three editor build modes, and universal base packaging.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
