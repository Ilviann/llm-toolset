# Development environment

This page records the local build and integration-test requirements for Unreal Editor MCP. It is development setup, not a model-facing runtime contract. Once implementation exists, executable metadata, build rules, runtime `capabilities`, and behavioral tests remain authoritative.

## Required software

- Unreal Engine 5.8 or newer with the host editor executable, public C++ headers, UnrealBuildTool, bundled .NET SDK, and platform build scripts installed. Support for a newer Unreal release must be demonstrated by the Phase 1 compilation probes and integration suite.
- Visual Studio with the Desktop development with C++ workload and an Unreal-supported MSVC and Windows SDK for mandatory native Windows validation. Confirm the exact installed SDK with AutomationTool Turnkey before compiling.
- Xcode 26.1.1 when performing preferred Unreal Engine 5.8 macOS follow-up validation. Xcode must have completed first-launch setup and license acceptance. Select this version per build rather than assuming the globally selected or newest Xcode is compatible. See Epic's [macOS development requirements](https://dev.epicgames.com/documentation/unreal-engine/macos-development-requirements-for-unreal-engine?lang=en-US).
- Python 3.10 or newer. Production code and tests use the standard library unless a later roadmap change explicitly authorizes and pins a dependency.
- A Windows host capable of running Unreal Engine 5.8 is required for release qualification. A macOS host is optional for preferred follow-up validation. Development and tests must remain usable on the repository's 16 GB reference machine.
- Enough local storage for Unreal-generated `Binaries`, `Build`, `Intermediate`, `Saved`, workspace, compiler, and Derived Data Cache output. Native build and test workflows must not require network downloads.

Native Windows qualification is the release gate. macOS validation is preferred but may occur after release. Linux is outside the current support and verification scope.

## Local path configuration

Configure these project-specific environment variables with absolute paths. Do not commit their values or any other machine-specific path.

| Variable | Required value |
| --- | --- |
| `UE58` | Installed Unreal Engine 5.8 root containing `Engine/`. |
| `XCODE26_1_1` | macOS only: Xcode 26.1.1 application directory, ending in `Xcode.app`. Repository tools append `Contents/Developer` before setting `DEVELOPER_DIR`. |

Derive the Unreal tools from `UE58`; do not configure separate paths for each executable:

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

Use `ue-test/ue58/` as the local Unreal Engine 5.8 project for plugin compilation, Unreal Automation Tests, command-line editor checks, and cross-process bridge integration. The descriptor must be at the fixed checkout-relative path `ue-test/ue58/UnrealMCPTest.uproject`; the headless runner resolves that path from its own application checkout and provides no environment-variable or command-line override. The parent `ue-test/` directory is ignored because Unreal regenerates substantial machine-specific state and can hold separate engine-version subfolders.

The test project must:

- use `EngineAssociation` 5.8;
- contain minimal C++ Game and Editor targets;
- use `BuildSettingsVersion.V7` and `EngineIncludeOrderVersion.Unreal5_8`;
- compile the `UnrealMCPTestEditor` target with the configured Launcher engine;
- remain disposable and contain no personal game content;
- create behavioral test assets at runtime rather than treating generated project state or prose documentation as fixtures.

Never run mutation, failure-recovery, or cleanup integration tests against a personal Unreal project.

## Setup verification

On macOS, run these checks from the `unreal-editor-mcp` application directory after configuring the applicable variables:

```sh
python3 --version
test -d "$UE58/Engine"
test -f "ue-test/ue58/UnrealMCPTest.uproject"
test -x "$UE58/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh"
test -x "$UE58/Engine/Build/BatchFiles/Mac/Build.sh"
test -x "$XCODE26_1_1/Contents/Developer/usr/bin/xcodebuild"
env DEVELOPER_DIR="$XCODE26_1_1/Contents/Developer" xcodebuild -version
env DEVELOPER_DIR="$XCODE26_1_1/Contents/Developer" xcodebuild -checkFirstLaunchStatus
```

On Windows PowerShell, run from the `unreal-editor-mcp` application directory, configure `UE58`, and verify the engine, fixed project, and Win64 SDK:

```powershell
python --version
Test-Path "$env:UE58\Engine"
Test-Path 'ue-test\ue58\UnrealMCPTest.uproject'
Test-Path "$env:UE58\Engine\Build\BatchFiles\Build.bat"
& "$env:UE58\Engine\Build\BatchFiles\RunUAT.bat" `
  Turnkey -command=VerifySdk -platform=Win64 -utf8output
```

Generate project files and compile the editor target before beginning or upgrading native plugin work. On macOS:

```sh
env DEVELOPER_DIR="$XCODE26_1_1/Contents/Developer" \
  "$UE58/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh" \
  -project="$PWD/ue-test/ue58/UnrealMCPTest.uproject" \
  -game

env DEVELOPER_DIR="$XCODE26_1_1/Contents/Developer" \
  "$UE58/Engine/Build/BatchFiles/Mac/Build.sh" \
  UnrealMCPTestEditor Mac Development \
  -Project="$PWD/ue-test/ue58/UnrealMCPTest.uproject" \
  -WaitMutex \
  -NoHotReloadFromIDE
```

On Windows PowerShell:

```powershell
& "$env:UE58\Engine\Build\BatchFiles\Build.bat" `
  UnrealMCPTestEditor Win64 Development `
  "-Project=$((Resolve-Path 'ue-test\ue58\UnrealMCPTest.uproject').Path)" `
  -WaitMutex `
  -NoHotReloadFromIDE
```

The direct Windows build does not require generated Visual Studio project files. UnrealBuildTool writes normal logs and caches outside the repository. Sandboxed development environments must explicitly permit those writes rather than redirecting or disabling Unreal's standard behavior.

`scripts/run_headless_integration.py` derives the fixed disposable project from its application checkout and derives the headless executable from the current host: the macOS app binary, `UnrealEditor-Cmd.exe` on Windows, and the Linux editor binary. Only macOS requires `XCODE26_1_1`; the runner appends `Contents/Developer`, validates `usr/bin/xcodebuild`, and forwards the result as `DEVELOPER_DIR`.

On Windows, run `python scripts/run_headless_integration.py --readonly-lifecycle-only` for the committed lifecycle-only acceptance against `UnrealEditor.exe`. It requires no pre-existing editor process and verifies the exact ten-tool catalog, access rejection, real launch/restart/shutdown, bridge replacement, and unchanged project-owned content.

## Binary plugin packaging

`scripts/package_plugin.py` invokes the configured engine's platform-appropriate `RunUAT` launcher with the standard `BuildPlugin` command. It accepts the engine only through `UE58` or the explicit `--engine-root` override, requires bounded `Build.version` metadata reporting Unreal Engine 5.8 or newer, keeps base and companion descriptors fixed to repository-owned paths, and passes every UAT argument as a subprocess array. After UAT completes, the wrapper restores every source-owned descriptor field while retaining UAT ownership of `Installed` and `EngineVersion`; verification rejects stripped companion API metadata, companion identity, load phase, default enablement, dependencies, or other source contracts. On macOS it requires `XCODE26_1_1` or the explicit `--developer-dir` override. The environment variable points to `Xcode.app`; the tool appends `Contents/Developer`, validates `usr/bin/xcodebuild`, and exports the derived path as `DEVELOPER_DIR` for the child build. The explicit override continues to accept a `Contents/Developer` directory.

From the application directory, package for the host's installed platforms with:

```sh
python3 scripts/package_plugin.py
```

The default destination is the workspace-level `build/unreal-editor-mcp` directory. AutomationTool clears this destination before packaging, so the wrapper rejects broad, source-overlapping, engine-overlapping, and other protected output paths before launch. After a successful UAT exit, the wrapper restores and verifies the complete bounded descriptor contract, requires `Installed: true`, and requires at least one file under `Binaries/`.

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
