# `gas-ability-blueprints` — Gameplay Ability Blueprint creation and editing

**Outcome:** Agents can create, inspect, configure, graph-edit, compile, save, and read back `UGameplayAbility`-derived Blueprint assets through an optional editor-only companion plugin without adding a Gameplay Ability System dependency to the base Unreal MCP plugin.

**Depends on:**

- [`phase-13`](phase-13.md)

### Companion and version contract

- Add a separate editor-only `UnrealMCPGAS` companion plugin that owns all direct dependencies on the Engine's Gameplay Ability System plugin and its `GameplayAbilities`, `GameplayTags`, and `GameplayTasks` modules. Add an editor module dependency only for public editor APIs that are exercised by compiled probes and native behavioral tests.
- Keep `UnrealMCP` independent of Gameplay Ability System headers, modules, and plugin enablement. The base plugin must build, package, load, and retain its complete non-GAS capabilities when `UnrealMCPGAS` or the Engine Gameplay Ability System plugin is absent.
- Reuse one narrow base-owned extension interface rather than adding another listener or credential. The companion may register only its explicit family policy, typed handlers, action contributors, and bounded capability data with the base bridge; it must reuse base authentication, Game-thread dispatch, operation ledger, errors, limits, and shutdown.
- Release `UnrealMCPGAS` in exact lockstep with `UnrealMCP`. Give both descriptors the same numeric `Version` and string `VersionName`; set the companion's required `UnrealMCP` plugin reference `RequestedVersion` to that exact numeric version; package both from the same source state in one release bundle.
- Extend release-contract tests to compare Python, native, base descriptor, and companion descriptor versions and the companion's requested base version. Publish companion presence, version, readiness, and supported GAS family capabilities through `capabilities`. Fail closed and expose no GAS mutation capability when the companion, base, Python server, Unreal build, or Engine GAS dependency is missing or mismatched.

### Ability Blueprint implementation

- Add an explicit `gameplay_ability` Blueprint family only while the verified companion capability is live. Accept native or Blueprint-generated parents only when they resolve to usable `UGameplayAbility` subclasses and pass the same stale-class, compilation, package, mount, and mutation-target policies as existing families.
- Reuse `blueprint_create`, `blueprint_inspect`, `blueprint_default_edit`, `blueprint_member_edit`, `blueprint_action_catalog`, `blueprint_graph_edit`, `blueprint_compile`, and `blueprint_save`. Do not publish a separate model-facing GAS tool or a general UObject/reflection editor.
- Inspect and mutate a small explicit allowlist of Gameplay Ability configuration proven against Unreal Engine 5.8 public APIs, including supported activation/instancing/network policies, Gameplay Tag and trigger containers, and compatible cost/cooldown Gameplay Effect class references. Give nested records stable identities where names or indexes are insufficient and require the authoritative asset snapshot for every mutation.
- Extend the action catalog with only context-valid Gameplay Ability events, functions, and Ability Task nodes returned by Unreal's live Blueprint action database. Preserve opaque action identities and existing graph/node/pin limits; do not synthesize arbitrary GAS calls or bypass the live K2 schema.
- Preserve unrelated graphs, members, defaults, inherited settings, tags, triggers, class references, comments, positions, bookmarks, and prior dirty state. Continue using explicit compilation and saving, bounded diagnostics, one transaction per accepted mutation, postcondition verification, retained operation reconciliation, and exact restoration on unexpected failure.
- Defer Gameplay Effect asset authoring to [`gas-gameplay-effects`](gas-gameplay-effects.md). Keep Attribute Set authoring, Gameplay Cue asset authoring, Ability System Component setup on Actors, input binding, project GAS configuration, runtime ability granting/activation, PIE execution, and arbitrary custom Ability Task or C++ generation outside this feature.

### Verification

- Prove the base plugin builds, packages, starts, and passes its complete suite with the companion absent and with the Engine Gameplay Ability System plugin disabled.
- Build and package the companion with normal, forced-unity, and non-unity editor builds. Test missing base, missing GAS dependency, disabled dependency, mismatched numeric versions, mismatched display versions, stale binaries, and unsupported Unreal builds without partially registering the family or handlers.
- Create representative abilities from native and Blueprint-generated parents. Inspect and edit each supported policy, tag/trigger form, and cost/cooldown reference; add representative context-valid ability events, ordinary graph nodes, and Ability Task nodes; compile, save, restart, and verify exact read-back.
- Test invalid parents and references, unsupported policies and GAS asset families, malformed or excessive tag/trigger collections, stale snapshots and identities, compile failure, transaction restoration, undo/redo, timeout, replay, lost-response recovery, and unchanged-content fingerprints.
- Verify capability registration and removal across editor restart and plugin enablement changes. Run the complete base and GAS companion suites natively on macOS and Windows against the supported Unreal Engine build.

### Documentation and completion gate

- Document companion installation and enablement, exact lockstep versioning, capability detection, supported Ability Blueprint configuration and graph workflows, limits, exclusions, recovery, and focused creation/edit examples.
- Document offline source and binary packaging of the base and companion from one release state. Never download or enable dependencies at runtime.
- Complete the feature only when a mismatched or absent companion cannot expose or execute GAS operations, the base plugin remains fully usable without GAS, and representative Gameplay Ability Blueprints can be created, configured, graph-edited, compiled, saved, restarted, and read back without altering unrelated content on both native platforms.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
