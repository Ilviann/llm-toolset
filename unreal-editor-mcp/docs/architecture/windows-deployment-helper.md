# Windows deployment helper

`scripts/deploy_plugin_windows.py` is the stable graphical entrypoint and re-exports the `scripts/windows_deployment/` library. `discovery.py` owns bounded project and Engine discovery; `verification.py` owns descriptor, module-rule, binary/symbol, precompiled-artifact, and forbidden-file checks; `transaction.py` owns staging, commit, final verification, project enablement, cleanup, and rollback; `configuration.py` owns LM Studio and ChatGPT Codex previews; and `workflow.py` owns typed planning, streamed package builds, and coordination. `view.py` constructs Tkinter widgets while `controller.py` owns validation, confirmation, background work, and events.

The workflow imports the `scripts/packaging/` service API, never the `package_plugin.py` CLI module. Both tools share fixed plugin identities and the Unreal Engine 5.8+ installation validator through `scripts/unreal_tooling/`.

## Invariants

- Packaging remains offline and fixed to the base descriptor plus independently selected GAS/CommonUI companions. All selected packages verify before installation.
- Binary filtering, precompiled rules, descriptor default state, destination confinement, reparse checks, stale-state checks, selected-set commit, rollback, and cleanup retain the documented deployment contract.
- Project and Engine destinations and `.uproject` updates are computed in a typed immutable plan before package builds. Execution rejects destination or descriptor drift before committing.
- Deployment core, transaction, verification, discovery, configuration, and workflow modules have no Tkinter dependency. Only the view/controller boundary interacts with the GUI event loop.
- One configuration definition still supplies both host previews, remains readonly by default, and never contains bridge credentials.

## Verification

`tests/test_deploy_plugin_windows.py` targets the owning discovery, workflow, transaction, verification, and configuration modules and checks the compatibility entrypoint. Coverage includes bounded parsing, Engine discovery/validation, fixed build commands, binary filtering, all install modes, three-plugin commit/rollback, descriptor drift, optional PDB enforcement, lifecycle validation, and exact previews. `tests/test_package_plugin.py` covers the shared packaging boundary. This support-tool decomposition does not require a native package build because package commands and verification behavior are unchanged.
