# Issue 3: `blueprint_action_catalog` rejects a current Blueprint snapshot

## Status

Open. Reproduced consistently on Windows with Unreal Engine 5.8.0 and Unreal MCP 0.22.0.

## Environment

- Unreal Engine: 5.8.0, changelist 55116800
- Unreal MCP: 0.22.0
- Platform: Windows 11
- Blueprint family: Actor
- Blueprint parent: native C++ `AManualTurret`

## Summary

`blueprint_action_catalog` returns `stale_precondition` when given the exact structural snapshot returned immediately beforehand by `blueprint_inspect`. A subsequent inspection returns the same snapshot, and the Blueprint remains clean, idle, and structurally unchanged.

Restarting Unreal Editor and obtaining a new bridge instance does not resolve the failure.

## Reproduction

1. Inspect the Blueprint:

   ```json
   {
     "mode": "inspect",
     "asset_path": "/Game/Core/Turrets/BP_ManualTurret.BP_ManualTurret",
     "graph_id": "5713dab34b895d3e01e2acbd77704dcc",
     "sections": ["summary", "graphs"],
     "page_size": 100
   }
   ```

2. Observe the current snapshot in the inspection result:

   ```json
   {
     "package_dirty": false,
     "node_count": 3,
     "snapshot_id": "81d090c51133f5b950e30b4b6401e97177256a29"
   }
   ```

3. Immediately query the action catalog with that snapshot:

   ```json
   {
     "asset_path": "/Game/Core/Turrets/BP_ManualTurret.BP_ManualTurret",
     "expected_snapshot": "81d090c51133f5b950e30b4b6401e97177256a29",
     "graph_id": "5713dab34b895d3e01e2acbd77704dcc",
     "limit": 20,
     "node_family": "event",
     "text": "FirePresentation"
   }
   ```

4. Observe the response:

   ```json
   {
     "code": "stale_precondition",
     "details": {},
     "message": "The Blueprint structural snapshot changed before cataloging",
     "retryable": false
   }
   ```

5. Inspect the Blueprint again and observe that the snapshot is still:

   ```text
   81d090c51133f5b950e30b4b6401e97177256a29
   ```

The failure also occurs for:

- Event: `FirePresentation`
- Function call: `SpawnSystemAttached`
- Function call: `Set Niagara Variable Position`
- Variable getter: `Muzzle`

Both individual and concurrent catalog requests fail in the same way.

## Expected result

`blueprint_action_catalog` accepts the current structural snapshot and returns matching graph actions.

If catalog initialization legitimately changes structural state, the response reports the new snapshot or marks the error as retryable so the caller can recover.

## Actual result

Every tested catalog query rejects the current snapshot with a non-retryable `stale_precondition`. The empty `details` object does not identify which structural value supposedly changed.

## Impact

- Blueprint graph authoring cannot use catalog-discovered action IDs for the affected Blueprint.
- Reinspection and editor restart do not provide a recovery path.
- The response does not expose enough diagnostic state to distinguish a real concurrent change from inconsistent snapshot calculation.

## Additional observations

- The Blueprint was not dirty, compiling, or running in PIE.
- Its status was `up_to_date`, and it remained structurally unchanged during the requests.
- `blueprint_inspect`, `blueprint_default_edit`, `blueprint_compile`, and `blueprint_save` worked correctly on the same assets.
- The behavior persisted after restarting Unreal Editor and obtaining a new bridge instance.
- The evidence suggests that `blueprint_inspect` and `blueprint_action_catalog` may calculate or validate the structural snapshot differently.

## Investigation

1. Compare the snapshot construction and precondition-validation paths used by `blueprint_inspect` and `blueprint_action_catalog`.
2. Capture the expected snapshot, the catalog's computed snapshot, and the structural input that differs when returning `stale_precondition`.
3. Determine whether action-database initialization or graph-context setup reconstructs any transient editor state before validation.
4. Reproduce the defect with event, function-call, and variable-get queries on an Actor Blueprint with a native C++ parent.
5. Add a regression test that passes an immediately inspected snapshot to the catalog and confirms that the Blueprint remains mutation-free.

## Workaround

Avoid Blueprint graph authoring for the affected presentation logic. Implement the default presentation in native C++ and use `blueprint_default_edit` to assign the Niagara asset.
