# Python support tooling

Repository support tools live under `scripts/` and remain standard-library-only, offline, bounded, and independent of the installed `unreal_editor_mcp` runtime package. The three public scripts are compatibility entrypoints only:

- `package_plugin.py` re-exports `packaging`, whose typed package requests flow through validation, command construction, descriptor restoration, execution, and output verification.
- `deploy_plugin_windows.py` re-exports `windows_deployment`. Discovery, verification, installation transactions, configuration previews, workflow coordination, Tk view construction, and controller event handling have separate owners. Deployment consumes the `packaging` library API rather than the packaging CLI entrypoint.
- `run_headless_integration.py` re-exports `headless_integration.lifecycle`. Process lifetime, Automation, capability contracts, operation reconciliation, cursor collection, domain scenarios, and typed Blueprint handoff state have separate owners.

`unreal_tooling` owns only primitives shared by two or more tools: bounded JSON/path helpers, Unreal Engine 5.8+ installation metadata, host launcher/executable paths, macOS Xcode application selection, and the fixed repository plugin catalog. On macOS, tools read `XCODE26_1_1` as an `Xcode.app` path, append `Contents/Developer`, validate `usr/bin/xcodebuild`, and replace inherited `DEVELOPER_DIR`; packaging's explicit `--developer-dir` override remains a direct developer-directory path. Import direction is entrypoint to owning package to `unreal_tooling`; shared modules never import workflow packages, Tkinter, scenarios, or the installed runtime package.

## Invariants

- Packaging, deployment, and headless process execution retain separate lifetime and failure contracts; no generic subprocess wrapper owns them.
- Deployment core modules import no Tkinter. `view.py` constructs widgets and passive updates; `controller.py` owns validation, confirmation, background work, and event dispatch.
- Deployment request, plan, and result records make selected plugins, destinations, prior state, descriptor updates, and default-enablement explicit before the transaction begins.
- Headless game data, level opening, level management, level editing, companion admission, GAS, CommonUI, and writable-fixture scenarios are focused modules. `game_data_levels.py` and `companions.py` remain compatibility facades.
- Blueprint fixture and authored handoffs use frozen mapping-compatible records grouped into declaration, family, replacement, node, pin, and aggregate state.
- Retried readiness, lost-response reconciliation, and cursor collection are bounded shared infrastructure. Mutation replay retains the original operation ID.
- Ambient `DEVELOPER_DIR` and the retired `UNREAL_MCP_DEVELOPER_DIR` do not satisfy the pinned macOS Xcode requirement.

## Verification

`tests/test_package_plugin.py`, `tests/test_deploy_plugin_windows.py`, and `tests/test_headless_integration.py` target owning library modules while checking compatibility entrypoints. The complete Python suite covers unchanged contracts. Windows completion additionally requires all native Automation cases and the complete two-process UE 5.8 headless workflow; packaging builds remain required only when package commands or verification behavior changes.
