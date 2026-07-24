# Development environment

This page records the local build and integration-test requirements for Unreal Editor MCP. It is development setup, not a model-facing runtime contract. Once implementation exists, executable metadata, build rules, runtime `capabilities`, and behavioral tests remain authoritative.

## Required software

- Unreal Engine 5.8 or newer with the host editor executable, public C++ headers, UnrealBuildTool, bundled .NET SDK, and platform build scripts installed. Support for a newer Unreal release must be demonstrated by the Phase 1 compilation probes and integration suite.
- Xcode 26.1.1 for the primary Unreal Engine 5.8 macOS baseline. Xcode must have completed first-launch setup and license acceptance. Select this version per build rather than assuming the globally selected or newest Xcode is compatible. See Epic's [macOS development requirements](https://dev.epicgames.com/documentation/unreal-engine/macos-development-requirements-for-unreal-engine?lang=en-US).
- Visual Studio with the Desktop development with C++ workload and an Unreal-supported MSVC and Windows SDK for native Windows validation. Confirm the exact installed SDK with AutomationTool Turnkey before compiling.
- Python 3.10 or newer. Production code and tests use the standard library unless a later roadmap change explicitly authorizes and pins a dependency.
- A macOS or Windows host capable of running Unreal Engine 5.8. Development and tests must remain usable on the repository's 16 GB reference machine.
- Enough local storage for Unreal-generated `Binaries`, `Build`, `Intermediate`, `Saved`, workspace, compiler, and Derived Data Cache output. Native build and test workflows must not require network downloads.

Native macOS validation comes first. Platform-specific discovery, path, process, and build behavior must remain isolated for mandatory native Windows qualification and Linux source portability.

## Local path configuration

Configure these project-specific environment variables with absolute paths. Do not commit their values or any other machine-specific path.

| Variable | Required value |
| --- | --- |
| `UNREAL_MCP_ENGINE_ROOT` | Installed Unreal Engine root containing `Engine/`. |
| `UNREAL_MCP_TEST_UPROJECT` | Disposable Unreal MCP test project's `.uproject` file under `ue-test/`. |
| `UNREAL_MCP_DEVELOPER_DIR` | macOS only: Xcode 26.1.1 `Contents/Developer` directory used for builds and headless tests. |

Derive the Unreal tools from `UNREAL_MCP_ENGINE_ROOT`; do not configure separate paths for each executable:

- `Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor`
- `Engine/Binaries/Win64/UnrealEditor-Cmd.exe`
- `Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh`
- `Engine/Build/BatchFiles/Mac/Build.sh`
- `Engine/Build/BatchFiles/Build.bat`
- `Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll`
- `Engine/Binaries/ThirdParty/DotNet/`

Development scripts must validate the configured paths before use, preserve paths containing spaces, and pass fixed argument arrays to subprocesses. These variables are developer inputs only. Model-facing tools must never accept executable paths, environment variables, shell fragments, or arbitrary build arguments.

The authenticated bridge token is not an environment variable. The Unreal plugin generates and durably persists a high-entropy token per test project and fails closed if that state cannot be secured and re-read.

## Disposable Unreal project

Use `ue-test/` as the local project for plugin compilation, Unreal Automation Tests, command-line editor checks, and cross-process bridge integration. The entire directory is ignored because Unreal regenerates substantial machine-specific state.

The test project must:

- use `EngineAssociation` 5.8;
- contain minimal C++ Game and Editor targets;
- use `BuildSettingsVersion.V7` and `EngineIncludeOrderVersion.Unreal5_8`;
- compile the `UnrealMCPTestEditor` target with the configured Launcher engine;
- remain disposable and contain no personal game content;
- create behavioral test assets at runtime rather than treating generated project state or prose documentation as fixtures.

Never run mutation, failure-recovery, or cleanup integration tests against a personal Unreal project.

## Setup verification

On macOS, run these checks from the repository root after configuring all three variables:

```sh
python3 --version
test -d "$UNREAL_MCP_ENGINE_ROOT/Engine"
test -f "$UNREAL_MCP_TEST_UPROJECT"
test -x "$UNREAL_MCP_ENGINE_ROOT/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh"
test -x "$UNREAL_MCP_ENGINE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh"
test -x "$UNREAL_MCP_DEVELOPER_DIR/usr/bin/xcodebuild"
env DEVELOPER_DIR="$UNREAL_MCP_DEVELOPER_DIR" xcodebuild -version
env DEVELOPER_DIR="$UNREAL_MCP_DEVELOPER_DIR" xcodebuild -checkFirstLaunchStatus
```

On Windows PowerShell, configure the two common variables and verify the engine, project, and Win64 SDK:

```powershell
python --version
Test-Path "$env:UNREAL_MCP_ENGINE_ROOT\Engine"
Test-Path $env:UNREAL_MCP_TEST_UPROJECT
Test-Path "$env:UNREAL_MCP_ENGINE_ROOT\Engine\Build\BatchFiles\Build.bat"
& "$env:UNREAL_MCP_ENGINE_ROOT\Engine\Build\BatchFiles\RunUAT.bat" `
  Turnkey -command=VerifySdk -platform=Win64 -utf8output
```

Generate project files and compile the editor target before beginning or upgrading native plugin work. On macOS:

```sh
env DEVELOPER_DIR="$UNREAL_MCP_DEVELOPER_DIR" \
  "$UNREAL_MCP_ENGINE_ROOT/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh" \
  -project="$UNREAL_MCP_TEST_UPROJECT" \
  -game

env DEVELOPER_DIR="$UNREAL_MCP_DEVELOPER_DIR" \
  "$UNREAL_MCP_ENGINE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh" \
  UnrealMCPTestEditor Mac Development \
  -Project="$UNREAL_MCP_TEST_UPROJECT" \
  -WaitMutex \
  -NoHotReloadFromIDE
```

On Windows PowerShell:

```powershell
& "$env:UNREAL_MCP_ENGINE_ROOT\Engine\Build\BatchFiles\Build.bat" `
  UnrealMCPTestEditor Win64 Development `
  "-Project=$env:UNREAL_MCP_TEST_UPROJECT" `
  -WaitMutex `
  -NoHotReloadFromIDE
```

The direct Windows build does not require generated Visual Studio project files. UnrealBuildTool writes normal logs and caches outside the repository. Sandboxed development environments must explicitly permit those writes rather than redirecting or disabling Unreal's standard behavior.

`scripts/run_headless_integration.py` derives the headless executable from the current host: the macOS app binary, `UnrealEditor-Cmd.exe` on Windows, and the Linux editor binary. Only macOS requires and forwards `UNREAL_MCP_DEVELOPER_DIR`.

## Binary plugin packaging

`scripts/package_plugin.py` invokes the configured engine's platform-appropriate `RunUAT` launcher with the standard `BuildPlugin` command. It accepts the engine only through `UNREAL_MCP_ENGINE_ROOT` or `--engine-root`, keeps the plugin descriptor fixed to `plugin/UnrealMCP/UnrealMCP.uplugin`, and passes every UAT argument as a subprocess array. On macOS it also requires `UNREAL_MCP_DEVELOPER_DIR`, `DEVELOPER_DIR`, or `--developer-dir` and exports the resolved value as `DEVELOPER_DIR` for the child build.

From the application directory, package for the host's installed platforms with:

```sh
python3 scripts/package_plugin.py
```

The default destination is the workspace-level `build/unreal-editor-mcp` directory. AutomationTool clears this destination before packaging, so the wrapper rejects broad, source-overlapping, engine-overlapping, and other protected output paths before launch. After a successful UAT exit, the wrapper requires the output descriptor to have `Installed: true` and requires at least one file under `Binaries/`.

Use `--target-platforms` with Unreal's `+`-separated platform names when the installed engine and host toolchain support an explicit target set. Use `--dry-run` to validate paths and show the exact command without changing the output. The workflow must remain offline; prepare every engine platform component and compiler toolchain before packaging.

## Initial verified baseline

The following combination generated project files and compiled the empty `UnrealMCPTestEditor` target successfully on 2026-07-21:

| Component | Verified value |
| --- | --- |
| Host | Apple Silicon, macOS 26.5.2, 16 GB memory |
| Unreal Engine | 5.8.0, changelist 55116800, Epic Games Launcher build |
| Xcode | 26.1.1, build 17B100 |
| Compiler and SDK | Apple clang 17.0.0, macOS SDK 26.1 |
| Python | CPython 3.14.6 |

This baseline is evidence for the current development host, not a compatibility promise. Re-run the public-API compilation probes and behavioral tests for every supported Unreal, Xcode, SDK, architecture, and platform combination.
