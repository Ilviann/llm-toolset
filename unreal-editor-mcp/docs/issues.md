# Unreal Engine and Xcode development issues

This page records toolchain and engine behavior encountered while implementing Phase 1 on 2026-07-21. The verified host was Apple Silicon macOS 26.5.2 with Unreal Engine 5.8.0 (changelist 55116800), Xcode 26.1.1, Apple clang 17.0.0, and the macOS 26.1 SDK. These observations are evidence for that environment, not claims about every Unreal or macOS installation.

## macOS is not covered by `PLATFORM_UNIX`

The first token-permission branch used only `PLATFORM_UNIX`. In this Unreal 5.8 macOS build that branch did not cover macOS, so `chmod(0600)` was skipped and the native permission check failed.

The compatibility adapter now uses `PLATFORM_UNIX || PLATFORM_MAC` for POSIX permission handling. Future platform branches must treat Unreal's named platform macros as authoritative and must not assume that `PLATFORM_UNIX` includes macOS.

## Directly piped headless editor output could abort before startup

Repeated command-line Automation Test launches exited with status 134 (`SIGABRT`) before Unreal initialized when the editor's stdout was attached directly to the development runner. The macOS diagnostic report stopped in AppKit application registration (`_RegisterApplication`/`NSApplication`), and no new Unreal project log was produced. Rebuilding the plugin did not change the result.

The same editor binary and test arguments ran successfully when stdout and stderr were captured to a temporary file by `subprocess.Popen`. `scripts/run_headless_integration.py --automation-only` uses that arrangement, reports the captured tail on failure, and verifies every expected native test result. This appears specific to the non-interactive launch environment; it is not established as an Unreal Engine defect or a plugin crash.

## HTTPServer listener ownership is process-wide

Unreal's `HTTPServer` module starts and stops listener sockets for the process, while individual consumers own routes on a shared per-port router. Calling `StopAllListeners` during this plugin's unload could therefore interrupt unrelated editor plugins.

Unreal MCP configures a per-port loopback listener override, verifies that the listener became active, owns and unbinds only `/unreal-mcp/v1/command`, and leaves process-wide listener shutdown to Unreal. The cross-process test separately proves that the configured port is unreachable through a non-loopback local interface.

## Deferred asset validation requires live dynamic mounts

Unreal 5.8 schedules validate-on-save work after `UPackage::SavePackage` returns. A native test that created a Blueprint in a dynamically registered local-plugin mount and immediately unregistered the mount triggered the localization validator because the saved asset no longer had a resolvable mount. Dynamic mutation tests keep a successful local-plugin mount registered through process shutdown; production project-plugin mounts already have editor/plugin lifetime.

## Abnormal termination can leave a discovery record

A forced or abnormal editor exit can bypass normal module shutdown and leave `Saved/UnrealMCP/discovery.json` behind. The record must never be treated as proof that the editor is still available.

The plugin removes any previous discovery record before its startup gate. The Python side also requires a fresh timestamp, matching project identity, bounded fields, and a live process ID before connecting. Normal module shutdown still removes the record immediately.

## Xcode selection was required, but no Xcode defect was found

UnrealBuildTool described Xcode 26.1.1 as a non-standard Xcode because it was selected from a project-specific location rather than assumed from the global developer directory. Supplying `DEVELOPER_DIR="$UNREAL_MCP_DEVELOPER_DIR"` consistently selected the intended compiler and SDK, and native compilation and linking succeeded.

No Xcode compiler, linker, code-signing, or SDK defect was encountered. The initial native compile corrections—matching Unreal's public forward declarations and using the correct ticker delegate-handle type—were plugin integration mistakes exposed by compilation, not Xcode behavior.
