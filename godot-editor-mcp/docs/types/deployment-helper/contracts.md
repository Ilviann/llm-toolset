# Deployment helper contracts

Use the index to retrieve only the contract section relevant to the task.

## Type: `ProjectInfo`

**Source:** `scripts/deploy_plugin.py`

Immutable pair of the resolved Godot project folder and its regular, bounded
`project.godot` descriptor. Construction rejects missing folders, missing or
linked descriptors, and descriptors larger than 4 MiB.

## Type: `DeploymentResult`

**Source:** `scripts/deploy_plugin.py`

Immutable successful result containing the installed addon destination,
confirmed enabled state, and whether `project.godot` required a change.

## Library: transactional addon deployment

**Source:** `scripts/deploy_plugin.py`

`deploy()` validates the bundled exact-version addon, builds a bounded
path/size/digest manifest, stages a physical copy beside the destination, and
backs up an existing addon only after staging verifies. It then publishes the
new addon, atomically enables `res://addons/godot_mcp/plugin.cfg`, and verifies
the installed manifest. Failure restores both previous artifacts. Replacement
requires an explicit caller decision; the UI obtains it through confirmation.

`enable_plugin_text()` supports a missing `[editor_plugins]` section, a missing
`enabled` key, or one single-line `PackedStringArray` of bounded string paths.
It rejects syntax it cannot preserve safely rather than rewriting arbitrary
Godot configuration.

## Library: MCP launch-definition generation

**Source:** `scripts/deploy_plugin.py`

`build_server_definition()` returns a JSON-compatible STDIO definition using
absolute existing paths for the checkout server, selected project, and current
Python interpreter. The mode is exactly `tiny`, `small`, or `large`; a selected
Godot executable adds `--godot-executable` only in `large`. `format_mcp_json()`
wraps that definition under the stable `godot-editor` server name.
