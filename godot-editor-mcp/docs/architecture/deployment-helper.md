# Deployment helper

## Purpose

Provide a dependency-free graphical path from a source checkout to an enabled,
exact-version Godot addon and usable LM Studio or Codex STDIO configuration.

## Owned source

- `scripts/deploy_plugin.py` — validation, transactional addon replacement,
  atomic plugin enablement, launch-definition generation, and Tkinter UI.
- `scripts/deploy_plugin.cmd` — Windows launcher.
- `scripts/deploy_plugin.sh` — macOS/Linux launcher.
- `tests/test_deploy_plugin.py` — focused deployment and generated-launch tests.

## Dependencies

The helper reads the bundled `plugin/addons/godot_mcp` tree, Python package
version, root `server.py`, and a user-selected Godot project. It produces the
same CLI contract owned by [Python entry and composition](python-entry-composition.md)
but is not imported by the MCP runtime.

## Invariants

- The selected root contains one regular, bounded `project.godot`; addon and
  descriptor boundaries reject symbolic links and Windows reparse points.
- Source, staged, and installed trees are bounded and compared by relative
  path, size, and SHA-256 digest. The bundled Python and plugin versions match.
- Replacement uses same-directory staging and backup names. Any failure before
  completion restores the prior addon and original project bytes.
- Enablement changes only the single-line `[editor_plugins] enabled` value,
  preserves other text, BOM, and line-ending style, and publishes atomically.
  Ambiguous, duplicate, oversized, or unsupported configuration fails closed.
- Launch definitions use absolute existing Python/server/project paths, expose
  only `tiny`, `small`, or `large`, default the UI to `small`, and accept a
  Godot executable only for `large` mode.
- The helper uses only the Python standard library and performs no downloads,
  network access, telemetry, or account operations.

## Change and verification guide

Review project confinement, destructive-path guards, rollback ordering,
configuration preservation, version consistency, platform wrappers, and README
instructions together. Run `tests.test_deploy_plugin`, `tests.test_contracts`,
the complete Python suite, and the documentation linter. Editor-side source is
not changed by deployment-helper-only work, so a headless Godot run is required
only when the bundled addon behavior also changes.
