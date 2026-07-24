# Windows deployment helper

`scripts/deploy_plugin_windows.py` owns the graphical source-checkout-to-project deployment workflow. Its tkinter UI selects one existing Unreal project folder and one Unreal Engine installation, runs the existing AutomationTool `BuildPlugin` contract for Win64, installs a binary-only `UnrealMCP` project plugin, and produces the exact LM Studio configuration for that project. `scripts/deploy_plugin_windows.cmd` is the Windows launcher.

The helper depends on `scripts/package_plugin.py` for trusted source-plugin identity, Engine structure validation, fixed AutomationTool arguments, protected output paths, and package verification. It reads only the selected folder's single immediate `.uproject` descriptor. The Engine input is initialized from trimmed `UNREAL_MCP_ENGINE_ROOT` and preserves that value after project selection when validation succeeds. Without a valid configured value, discovery prefers the selected project's exact `EngineAssociation` in Epic Launcher/user-build registry records and the conventional Epic Games installation path. A user-selected Engine folder remains authoritative after its `Build.version` confirms Unreal Engine 5.8 or newer.

## Invariants

- Packaging is offline, targets only `Win64`, embeds the selected Engine version, and uses the fixed repository plugin descriptor.
- The deployment keeps the packaged descriptor, Win64 DLL, `UnrealMCP.Build.cs` module rule, and precompiled build metadata, but omits C++ implementation/header source, external symbol/debug artifacts, object files, and debug-symbol bundles. The installed module rule explicitly sets `bUsePrecompiled = true` so a later C++ game-project build consumes rather than replaces the packaged module.
- The installed descriptor must have `Installed: true`, a Win64 plugin DLL must exist, and no filtered debug artifact may remain.
- Installation is confined to `<SelectedProject>/Plugins/UnrealMCP`. Reparse-point plugin paths and destinations outside the resolved project root reject.
- An existing plugin is replaced only after explicit GUI confirmation. Copy and verification happen in a new sibling staging directory; a temporary rename preserves the old plugin until the new directory is in place and verified.
- Build failure never changes the project's existing plugin.
- The generated JSON invokes the current Python executable, the checkout's fixed `server.py`, and the selected absolute `.uproject` path. It does not include the bridge token.
- AutomationTool output is streamed into the GUI without blocking tkinter's event loop. The window cannot close while deployment is active.

## Verification

`tests/test_deploy_plugin_windows.py` covers bounded project parsing, exact Engine-association candidate selection, 5.8+ validation, fixed Win64 arguments, binary filtering, guarded precompiled-module-rule configuration, precompiled-metadata retention, explicit replacement, non-mixing replacement, failed-verification restoration, result verification, and exact LM Studio JSON. `tests/test_package_plugin.py` continues to cover the shared packaging boundary. A native plugin build is required when the source plugin or its packaging compatibility changes; the helper itself has no Unreal runtime behavior.
