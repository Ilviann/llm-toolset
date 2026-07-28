# Issue 2: Unreal Editor can crash during a requested restart

## Status

Open. Awaiting crash logs and reproduction details.

## Summary

The Unreal Editor can sometimes crash when Unreal MCP requests an editor restart through the `editor_lifecycle` tool. The failure is intermittent, and the affected Unreal Engine version, host platform, lifecycle phase, and crash cause are not yet established.

## Expected result

A restart gracefully shuts down the configured editor, waits for the old process to exit, launches the same configured project, and reports readiness after rediscovery and reauthentication.

## Actual result

The Unreal Editor sometimes crashes while processing the requested restart.

## Impact

- Restart cannot be relied upon to complete safely in every observed run.
- The durable lifecycle record may require reconciliation after a crash or MCP server interruption.
- Callers must inspect the configured editor and retained lifecycle state before retrying an operation whose outcome is unknown.

## Investigation

1. Collect the Unreal crash report, editor log, MCP diagnostics, durable `Saved/UnrealMCP/lifecycle.json` record, operation ID, Unreal Engine version, host platform, and whether PIE, compilation, saving, transactions, or dirty packages were active.
2. Identify whether the crash occurs before graceful shutdown acceptance, during editor teardown, after the old process exits, during relaunch, or during bridge rediscovery and reauthentication.
3. Reproduce the failure against a disposable project and correlate the lifecycle phase with the native crash stack.
4. Determine whether the defect is in Unreal MCP's shutdown request or lifecycle coordination, the project/plugin teardown path, or Unreal Engine itself.
5. Add the smallest fix that preserves graceful-shutdown refusal rules, process/project identity checks, bounded waits, cancellation semantics, and the prohibition on forced termination.

## Verification required

- Add a regression test that exercises the confirmed failing lifecycle phase and fails before the fix.
- Run the focused Python lifecycle tests and the full Python suite.
- Build the disposable Editor target and run `UnrealMCP.Lifecycle`.
- Reproduce restart through the production bridge on the affected native platform and Unreal Engine version.
- Verify successful restart, abnormal-termination reporting, durable-record reconciliation, and safe retry guidance.

