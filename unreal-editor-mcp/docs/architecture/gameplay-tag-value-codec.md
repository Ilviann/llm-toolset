# Gameplay Tag value codec

## Ownership

`UnrealMCPGameplayTagValueCodec` in `UnrealMCPBlueprint` owns the semantic wire boundary for exact `FGameplayTag` and `FGameplayTagContainer` reflected values. `UnrealMCPPropertyCodec` uses it for direct editable properties, while `UnrealMCPGameDataValueCodec` uses it recursively inside already-supported structs and collections.

## Dependency direction

The adapter depends on the engine `GameplayTags` module, neutral wire values, and base-plugin limits. It does not depend on `GameplayAbilities`, `GameplayTasks`, the GAS companion, Gameplay Tag configuration, or any model-facing schema. Owning property workflows retain their existing admission, transaction, snapshot, saving, paging, and visibility policies.

## Invariants

- One tag is an exact string; an empty tag is `""`.
- One container is a case-sensitive sorted array of explicit stored tag names. Derived parent caches are never emitted.
- Writes accept only syntactically valid exact canonical names registered in the live tag tree. Unknown, redirected, non-canonical, duplicate, empty-container-item, overlength, and over-count inputs reject before assignment.
- Reads preserve exact stored names, including bounded legacy-invalid names, without asking the manager to redirect or repair them.
- Tag names are limited to 256 characters and containers to 64 explicit tags. An oversized stored value becomes unsupported or returns a bounded limit error instead of truncating.

## Verification

`UnrealMCP.GameplayTagProperties.CodecValidation` covers exact/empty values, explicit and derived parents, deterministic order, unknown/malformed/redirected/duplicate inputs, limits, atomic rejection, and legacy-invalid inspection. `UnrealMCP.GameplayTagProperties.BlueprintDefaultsAndComponents` covers Blueprint class and component defaults, stale snapshots, Undo/Redo, compile, and save. `UnrealMCP.Phase17.GameDataAuthoring` covers direct and nested Data Table values plus rejection preservation.
