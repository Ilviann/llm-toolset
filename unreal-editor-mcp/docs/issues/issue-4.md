# Issue 4: macOS lifecycle readiness times out after editor launch

## Status

Open. Reproduced consistently on macOS with Unreal Engine 5.8.1.

## Summary

The lifecycle-only native acceptance cannot complete on macOS. The committed command currently rejects non-Windows hosts, and invoking its otherwise host-neutral acceptance helper with the supported macOS `UnrealEditor` app binary consistently returns a bridge timeout during the initial lifecycle `launch` operation.

## Environment

- Apple Silicon macOS 26.5.2.
- Unreal Engine 5.8.1, changelist 56057345.
- Xcode 26.1.1, build 17B100.
- CPython 3.14.6.
- Disposable `ue-test/ue58/UnrealMCPTest.uproject` project.

## Expected result

The readonly+lifecycle MCP server launches the configured editor, observes an authenticated ready bridge, verifies the exact ten-tool catalog and writable-tool rejection, restarts into a new bridge instance, shuts down cleanly, and proves project-owned content is unchanged.

## Actual result

The editor process starts and publishes discovery, but the lifecycle `launch` operation returns the stable `timeout` bridge error before reporting readiness. The failure reproduced twice from a clean stopped state. After each failure, the launched editor remained alive; a later direct authenticated `editor_shutdown` call succeeded and reported zero dirty packages.

The full macOS production integration, readonly tool-content preservation, native `UnrealMCP.Readonly.PreservationAcrossReadonlyFlows` Automation test, adaptive and true forced-unity builds, and universal base packaging all pass. The failure is isolated to the separate lifecycle-only launch/readiness acceptance.

## Impact

- `readonly-mode` remains in the macOS native platform test backlog.
- macOS content-readonly behavior is verified, but configured lifecycle launch/restart cannot yet be claimed as natively accepted.
- A failed lifecycle launch can leave the disposable editor running and require a later graceful shutdown.

## Investigation

1. Make `scripts/run_headless_integration.py --readonly-lifecycle-only` select the supported macOS app binary as well as Windows `UnrealEditor.exe` while continuing to reject Linux.
2. Trace the interval between discovery publication, HTTP bridge readiness, and the first lifecycle `_verify_bridge` call.
3. Determine whether lifecycle launch must retry transient authenticated bridge timeouts until the existing bounded startup deadline instead of failing on the first discovery-backed call.
4. Preserve exact process ID, project hash, bridge version and instance checks while adding any readiness retry.
5. Ensure every failed acceptance path gracefully shuts down an editor that the helper launched.

## Verification required

- Add focused resolver and lifecycle-readiness regression tests that fail before the fix on the macOS branch.
- Run `python -m unittest tests.test_lifecycle tests.test_headless_integration -v` and the full Python suite.
- Run the normal and true forced-unity macOS editor builds and full native `UnrealMCP` Automation suite.
- Run `python scripts/run_headless_integration.py --readonly-lifecycle-only` on macOS and verify launch, restart, shutdown, bridge-instance replacement, writable-tool rejection, and unchanged project-owned content.
- Re-run the full production headless integration and confirm no live editor or discovery heartbeat remains.
