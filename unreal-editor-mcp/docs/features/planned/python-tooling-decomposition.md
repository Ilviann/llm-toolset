---
feature_id: python-tooling-decomposition
status: planned
depends_on:
  - windows-deployment-validation-fixes
released_in: null
release_track: support-tooling
---

# `python-tooling-decomposition` — Python support-tool responsibility decomposition

**Outcome:** Repository Python tools under `scripts/` have narrow ownership, thin stable entrypoints, typed scenario state, and reusable Unreal-local support modules without changing runtime, CLI, packaging, deployment, or cross-process behavior.

**Implementation status:** Planned as support tooling. It does not change Unreal MCP or companion semantic versions, the companion API, extension schemas, model-facing tools, or native plugin behavior.

**Depends on:**

- [`windows-deployment-validation-fixes`](../completed/windows-deployment-validation-fixes.md)

## Scope and ownership

Keep shared code inside `unreal-editor-mcp/scripts`; do not create a workspace-wide Python dependency.

- `scripts/unreal_tooling/` owns bounded low-level I/O, validated Engine installations, fixed repository plugin identities, and other primitives genuinely shared by two or more Unreal support tools.
- `scripts/packaging/` owns package requests, UAT `BuildPlugin` commands, descriptor restoration, output verification, and the package CLI service.
- `scripts/windows_deployment/` owns project and Engine discovery, binary-package filtering, installation transactions, configuration previews, workflow coordination, and the Windows view/controller.
- `scripts/headless_integration/` retains cross-process ownership while separating editor processes, Automation runs, capability contracts, lost-response reconciliation, cursor collection, and domain scenarios.
- `scripts/package_plugin.py`, `scripts/deploy_plugin_windows.py`, and `scripts/run_headless_integration.py` remain thin stable compatibility entrypoints.

Import direction is entrypoint to owning package to Unreal-local shared primitives. Shared tooling must not import deployment, packaging CLI, headless scenarios, Tkinter, or the installed runtime package. The installed `unreal_editor_mcp` package must not depend on repository scripts.

## Planned decomposition

### Shared Unreal tooling

- Add one typed Engine-installation boundary for bounded `Build.version` validation, host-specific UAT launchers, host-specific Editor executables, and macOS developer-directory configuration.
- Add one fixed plugin catalog for the base, fixture, GAS, and CommonUI descriptors and dependency relationships.
- Share only identical bounded JSON, path-containment, and reparse-point primitives. Keep project descriptor policy and destructive-path decisions in their owning workflows.
- Do not generalize process execution: package builds, streamed deployment builds, Automation runs, and retained Editor processes have different lifetime and failure contracts.

### Packaging and Windows deployment

- Move packaging behavior out of the CLI entrypoint so deployment imports a library API rather than `package_plugin.py`.
- Split binary verification into descriptor, module-rule, binary/symbol, precompiled-artifact, and forbidden-file checks.
- Represent deployment with typed request, plan, and result records. Split staging, commit, final verification, project enablement, cleanup, and rollback into an explicit selected-plugin transaction.
- Separate Tkinter widget construction from validation, confirmation, background work, and event handling. Core deployment workflows must have no Tkinter dependency.

### Headless integration infrastructure

- Move Editor resolution, launch, readiness, shutdown, and cleanup to one process-lifecycle module.
- Move Automation command construction, expected-case mapping, bounded logs, timeouts, and fixture preparation to one Automation module.
- Move feature, family, operation, and limit checks out of the main lifecycle workflow into a data-driven capability-contract module.
- Share lost-response reconciliation, retry-when-ready behavior, and bounded cursor-page collection across scenarios.

### Headless domain scenarios

- Split game data, level open/inspect, level management, and level editing into separate modules while keeping a temporary `game_data_levels.py` compatibility facade.
- Split companion admission, GAS inspection, CommonUI inspection, and the writable test companion into focused scenario modules.
- Organize Blueprint code by behavior rather than historical phase number: fixture preparation, declarations, family fixtures, logic-unit replacement, node lifecycle, and pin/connection editing.
- Replace the current large untyped Blueprint handoff dictionary with typed declarations, family, replacement, node, pin, and aggregate scenario-state records.
- Keep each Blueprint subscenario's authoring and restart verification together so persistence checks remain owned by the behavior that creates the state.
- Keep the already-cohesive asset, Widget, and readonly scenarios intact until evidence shows another responsibility boundary.

## Implementation checklist

- [ ] Add Unreal-local shared tooling and compatibility re-exports without behavior changes.
- [ ] Extract packaging services and remove deployment's dependency on the packaging CLI module.
- [ ] Split Windows deployment core, transaction, configuration, view, and controller responsibilities.
- [ ] Extract headless process, Automation, capability, operation, and pagination infrastructure.
- [ ] Split level/game-data and companion scenarios behind compatibility facades.
- [ ] Split Blueprint scenarios and introduce typed scenario state incrementally.
- [ ] Migrate tests from entrypoint internals and exact old source-file locations to owning module contracts.
- [ ] Update automated-verification, deployment, packaging, and type documentation for the final boundaries.

## Invariants and completion gates

- Preserve command-line options, environment variables, exit codes, stable errors, UAT argument arrays, output paths, generated configuration, install destinations, filtering, stale-state checks, rollback, and cleanup behavior.
- Preserve the exact released cross-process scenario coverage, operation reconciliation, saved-state handoff, restart checks, and public compatibility imports.
- Keep every filesystem operation bounded, root-confined, reparse-safe where currently required, offline, standard-library-only, and free of runtime downloads or telemetry.
- Run focused packaging, deployment, and headless Python tests after each extraction and the complete Python suite at each coherent phase.
- Run the documentation linter after Markdown changes. Update the contract test that currently reads `game_data_levels.py` when level editing moves to its owning module.
- Before completion, run applicable Windows Automation and complete headless cross-process validation against the disposable UE 5.8 project. A native package build is required only if packaging commands, compatibility, or verification behavior changes rather than moving unchanged code.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
