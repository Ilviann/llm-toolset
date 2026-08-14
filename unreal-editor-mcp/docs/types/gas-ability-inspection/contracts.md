# Gameplay Ability inspection contracts

## Family, records, and bounds

An admitted `unreal-mcp-gas` schema-revision-1 contribution adds the `gameplay_ability` family for usable native or Blueprint-generated `UGameplayAbility` descendants. Discovery remains non-loading and uses the native-parent Asset Registry tag; exact inspection validates the loaded generated class and class default object.

The retained native `gameplay_ability` handler produces four internal records. They are dormant in 0.36.0 pending a redesigned model-facing read contract:

- `gameplay_ability_policies` contains instancing, replication, network execution/security, remote-cancellation, retrigger, and input-replication values.
- `gameplay_ability_tags` contains sorted effective asset, cancel, block, activation, source, and target tag containers.
- `gameplay_ability_triggers` contains tag/source pairs with 32-character identities derived from semantic value plus duplicate ordinal.
- `gameplay_ability_effects` contains resolved class and asset identities for compatible cost and cooldown Gameplay Effect references.

Every field reports `source: local` or `source: inherited` by exact property comparison with the parent class default object. Tag output returns at most 256 tags per container and scans at most 2,048; trigger output returns at most 128 records and scans at most 1,024. Output reports total counts and truncation. The contribution returns at most four records, and the base inspector still enforces its 4,096-record structural ceiling, 100-record page ceiling, 32 cursors, and 30-second cursor lifetime.

The companion hashes all bounded effective typed state into the base Blueprint snapshot. Cursor continuation therefore returns `stale_precondition` when a GAS policy, tag, trigger, or effect reference changes, even when that value was outside the visible truncated prefix.

## Capabilities and exclusions

The family is published only when `UnrealMCPGAS`, Engine `GameplayAbilities`, all three required GAS modules, companion API v2, schema revision 2, and the matching Python catalog are ready. Native companion records publish semantic version, API/schema values, dependency state, stable limits, read support true, and mutation support false.

Only discovery and inspection are supported. Creation, compilation, saving, default/member/graph mutation, action catalog additions, runtime granting or activation, PIE execution, Attribute Set authoring, Gameplay Cue authoring, and arbitrary Ability Task or C++ generation are excluded.
