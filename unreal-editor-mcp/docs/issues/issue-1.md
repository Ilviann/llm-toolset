# Issue 1: Created Blueprint snapshot changes after first Windows restart

## Status

Resolved in 0.17.1 and verified on Windows with Unreal Engine 5.8.

## Summary

A Blueprint created, edited, compiled, and saved through the production Unreal MCP bridge previously returned one structural snapshot before shutdown and a different snapshot after the first clean editor restart.

This is not an editor-launch or Windows SDK failure. The Win64 SDK is valid, the plugin builds, all 30 native Unreal Automation cases pass, both editor processes start successfully, and the restarted process can inspect the saved asset.

## Reproduction

1. Configure `UE58` for Unreal Engine 5.8 and point `UNREAL_MCP_TEST_UPROJECT` at `ue-test/ue58/UnrealMCPTest.uproject`.
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

## Root cause

UE 5.8 regenerates the GUID of one hidden `ErrorTolerance` input pin on a specialized `K2Node_PromotableOperator` during the first asset reload. The pin is untyped (`PinCategory` is `None`), unlinked, and has no string, object, or text default. The graph, node, three real operator pins, defaults, and connections remain identical. The changing pin contributed one otherwise identical fingerprint line, so its regenerated GUID alone changed the SHA-1 snapshot.

## Resolution

Inspection and graph-edit result encoding now omit only this exact editor-derived pin state: a hidden, untyped, unlinked, default-free `ErrorTolerance` input owned by `K2Node_PromotableOperator`. Graph edits cannot resolve that omitted identity, and changed-node pin counts, bounds, created identities, and reconstruction tracking use the same canonical pin set. A typed, visible, linked, or default-bearing tolerance pin remains model-facing and structural.

The exact snapshot assertion remains unchanged. Native coverage reproduces the derived pin, verifies it is omitted, and proves changing only its GUID cannot change the snapshot. The full Windows production bridge workflow now retains the exact post-save snapshot across the first clean restart.

## Verification

- Normal adaptive Win64 editor build.
- Forced-unity Win64 editor build.
- Python boundary suite.
- Native Unreal Automation, including the Phase 13 regression.
- Full Windows cross-process workflow with exact post-save-to-first-restart equality.
