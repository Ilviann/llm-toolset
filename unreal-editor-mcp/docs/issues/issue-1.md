# Issue 1: Created Blueprint snapshot changes after first Windows restart

## Status

Open and reproducible on Windows with Unreal Engine 5.8.

## Summary

A Blueprint created, edited, compiled, and saved through the production Unreal MCP bridge returns one structural snapshot before shutdown and a different snapshot after the first clean editor restart. The cross-process acceptance workflow requires these snapshots to match exactly and fails when they differ.

This is not an editor-launch or Windows SDK failure. The Win64 SDK is valid, the plugin builds, all 30 native Unreal Automation cases pass, both editor processes start successfully, and the restarted process can inspect the saved asset.

## Reproduction

1. Configure `UNREAL_MCP_ENGINE_ROOT` for Unreal Engine 5.8 and `UNREAL_MCP_TEST_UPROJECT` for the disposable test project.
2. From the `unreal-editor-mcp` directory, run:

   ```powershell
   python scripts/run_headless_integration.py
   ```

3. Allow the first editor process to create, edit, compile, and save the production Blueprint fixture.
4. Observe the fixture again after the workflow restarts the editor.

## Expected result

The snapshot returned after the save matches the snapshot produced by inspection after the first clean editor restart.

## Actual result

The restarted editor returns a different snapshot, and the workflow terminates with:

```text
AssertionError: created Blueprint snapshot changed after editor restart
```

## Impact

- The complete Windows cross-process acceptance workflow does not pass.
- Persistence of the authored Blueprint cannot currently be certified against the released exact-snapshot contract on its first reload.
- Python boundary tests, native Unreal Automation tests, and Windows compilation remain usable and passing.

## Current workaround

There is no accepted workaround that preserves the exact post-save-to-first-reload contract. Do not replace the equality assertion with a weaker check. The fixed Phase 2 persistence fixture may use its first loaded snapshot as the baseline for a second restart, but that behavior does not satisfy the stricter production-created Blueprint contract.

## Investigation notes

The failure is isolated to snapshot stability after persistence. Likely investigation areas include Unreal save-time versus load-time normalization, reconstructed graph or pin identities, serialized default values, and snapshot fields whose canonical representation changes on initial reload. The differing snapshot inputs must be identified before changing production behavior or the contract.

## Resolution criteria

- Identify and document the exact snapshot fields that change.
- Correct save, load, inspection, or canonicalization behavior without hiding a meaningful structural change.
- Pass the full Windows cross-process workflow with an exact post-save-to-first-restart snapshot match.
- Preserve all Python and native Unreal Automation results.

