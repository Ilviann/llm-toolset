# Editor lifecycle

## Ownership

`unreal_editor_mcp/lifecycle.py` owns the independently optional `editor_lifecycle` tool, configured editor launch, readiness/shutdown waits, cancellation, and the durable lifecycle record. `platforms.py` owns detached macOS and Windows process construction. The native bridge owns graceful-shutdown safety and the exit request.

## Dependency direction

The CLI validates the optional absolute editor executable and injects one `EditorLifecycle` beside the existing authenticated `UnrealBridge`. The lifecycle controller may launch only that executable with the already resolved `.uproject`; model arguments contain only an operation ID and a typed operation. Restart composes authenticated shutdown, old-process exit, configured launch, discovery, reauthentication, and exact bridge-version/instance checks.

## Invariants

- `editor_lifecycle` is absent unless `--editor-lifecycle <absolute-executable>` validates and constructs the controller. Lifecycle availability and readonly/writable project-content access are independent.
- No tool argument can select an executable, project, environment value, process, port, shell fragment, forced exit, or arbitrary argument.
- Windows launches only an absolute `UnrealEditor.exe`; macOS launches only an executable `UnrealEditor` app binary. Linux command construction rejects without claiming native launch support.
- Only one lifecycle operation runs at a time. Launch and shutdown waits are bounded to 5–900 configured seconds; cancellation stops waiting but never force-terminates an editor.
- Readiness requires the configured project hash, launched process ID, exact Python/plugin version, authenticated bridge, and a new bridge instance after restart.
- Native shutdown refuses PIE/simulation, saving, garbage collection, transactions, compiling assets, and any dirty package. It never saves, discards, prompts, or force-kills.
- `Saved/UnrealMCP/lifecycle.json` retains at most 16 records for 24 hours using bounded atomic writes. Nonterminal records loaded by a new server become `outcome_unknown`.

## Verification

Run `python -m unittest tests.test_lifecycle -v`, the full Python suite, compile the disposable Editor target, run `UnrealMCP.Lifecycle`, then verify launch/shutdown/restart against a disposable project on macOS and Windows.
