---
feature_id: godot-deployment-helper
status: completed
depends_on: []
released_in: "godot-editor-mcp 0.18.0"
---

# `godot-deployment-helper` — Godot MCP deployment helper

## Summary

Godot Editor MCP now provides a portable Tkinter helper that installs or
updates the bundled addon, enables it in the selected project, and emits
copyable LM Studio and Codex STDIO configuration. The UI exposes all three tool
modes with `small` preselected and supports an optional large-mode Godot
executable.

Deployment is offline, bounded, link-safe, exact-version, and transactional.
Failed publication or verification restores the previous addon and original
`project.godot` bytes.

## Direct prerequisites

None.

## Completion checklist

- [x] Add portable graphical installation and configuration generation.
- [x] Add automatic, atomic addon enablement.
- [x] Add bounded staging, verification, replacement, and rollback.
- [x] Add Windows and POSIX launch wrappers.
- [x] Add normal, invalid, limit, security, rollback, and subprocess tests.
- [x] Update architecture, contracts, user setup, history, and versions.
