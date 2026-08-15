# Issue 4: macOS lifecycle launch exits before bridge readiness

## Status

Open. Reproduced consistently on macOS with Unreal Engine 5.8.1.

## Summary

The lifecycle-only native acceptance cannot complete on macOS. The committed command currently rejects non-Windows hosts, and invoking its otherwise host-neutral acceptance helper with the supported macOS `UnrealEditor` app binary fails during the initial lifecycle `launch` operation before an authenticated ready bridge is observed.

## Environment

- Apple Silicon macOS 26.5.2.
- Unreal Engine 5.8.1, changelist 56057345.
- Xcode 26.1.1, build 17B100.
- CPython 3.14.6.
- Disposable `ue-test/ue58/UnrealMCPTest.uproject` project.

## Expected result

The readonly+lifecycle MCP server launches the configured editor, observes an authenticated ready bridge, verifies the exact ten-tool catalog and writable-tool rejection, restarts into a new bridge instance, shuts down cleanly, and proves project-owned content is unchanged.

## Actual result

The 2026-08-15 acceptance attempt returned the stable `editor_unavailable` lifecycle error because the configured editor exited with return code 0 before its bridge became ready. The committed `--readonly-lifecycle-only` entrypoint separately rejected macOS as Windows-only. No editor process, listener, or discovery heartbeat remained after the failed attempt.

The full macOS production integration, readonly tool-content preservation, native `UnrealMCP.Readonly.PreservationAcrossReadonlyFlows` Automation test, adaptive/forced-unity/non-unity builds, and universal base packaging all pass. The failure is isolated to the separate lifecycle-only launch/readiness acceptance.

## Impact

- `readonly-mode` remains in the macOS native platform test backlog.
- macOS content-readonly behavior is verified, but configured lifecycle launch/restart cannot yet be claimed as natively accepted.
- The current failed lifecycle launch exits cleanly before readiness, so restart and shutdown assertions cannot run.

## Investigation

1. Make `scripts/run_headless_integration.py --readonly-lifecycle-only` select the supported macOS app binary as well as Windows `UnrealEditor.exe` while continuing to reject Linux.
2. Trace the configured editor's clean early exit and the interval between process launch, discovery publication, HTTP bridge readiness, and the first lifecycle `_verify_bridge` call.
3. Determine whether lifecycle launch must retry transient discovery/readiness states until the existing bounded startup deadline.
4. Preserve exact process ID, project hash, bridge version and instance checks while adding any readiness retry.
5. Ensure every failed acceptance path gracefully shuts down an editor that the helper launched.

## Verification required

- Add focused resolver and lifecycle-readiness regression tests that fail before the fix on the macOS branch.
- Run `python -m unittest tests.test_lifecycle tests.test_headless_integration -v` and the full Python suite.
- Run the normal and true forced-unity macOS editor builds and full native `UnrealMCP` Automation suite.
- Run `python scripts/run_headless_integration.py --readonly-lifecycle-only` on macOS and verify launch, restart, shutdown, bridge-instance replacement, writable-tool rejection, and unchanged project-owned content.
- Re-run the full production headless integration and confirm no live editor or discovery heartbeat remains.
