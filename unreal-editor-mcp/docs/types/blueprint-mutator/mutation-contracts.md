# Creation, compile, and save contracts

`blueprint_create` accepts exactly `parent_class` and `package_path`. `parent_class` is a bounded Unreal class object path such as `/Script/Engine.Actor` or `/Game/Actors/BP_Base.BP_Base_C`. `package_path` is a long package name without an object suffix, such as `/Game/Actors/BP_Door`.

The parent must resolve to a usable Actor-derived Blueprint base. Native parents must pass Unreal's live Blueprint-base policy. Blueprint-generated parents must not be skeleton, reinstanced, compiling, or in an error state. Abstract, deprecated, newer-version, editor-only, missing, and non-Actor classes reject as `invalid_parent`.

`blueprint_compile` and `blueprint_save` each accept exactly one `asset_path`. Package paths and canonical object paths are accepted; object names must match the package asset name. The asset must be an Actor Blueprint inside the mutation scope.

The mutation scope is `/Game` plus content mounts physically below the current project's local `Plugins/` directory and owned by an ancestor `.uplugin` descriptor. Existing path segments from the trusted root to the destination must not be symlinks. `/Engine`, external engine/marketplace plugins, arbitrary dynamic mounts, raw filesystem paths, traversal, and unavailable mounts reject as `mutation_scope_denied` or `invalid_argument`.

Every successful operation returns `asset_path`, `parent_class`, `compile_state`, `compile_succeeded`, `saved`, `package_dirty`, `snapshot_id`, `diagnostics`, `diagnostic_count`, and `diagnostics_truncated`. Each diagnostic contains only `severity` (`error`, `warning`, or `note`) and a bounded `message`. At most 64 messages of 512 characters each are returned.

An explicit compiler run that finds Blueprint errors is a successful tool operation with `compile_succeeded: false`; this preserves its structured diagnostics. Creation requires a successful compile and instead returns `compile_failed` while removing the unpublished asset. Other stable mutation errors include `already_exists`, `write_conflict`, and `save_failed`. Transport, authentication, timeout, version, and response-limit errors remain distinct bridge failures.
