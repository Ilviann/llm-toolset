---
feature_id: engine-plugin-removal
status: completed
depends_on: []
released_in: null
release_track: support-tooling
---

# `engine-plugin-removal` — Windows engine plugin removal

**Outcome:** A single CMD script removes installed Unreal MCP base and companion folders from the `UE58` engine.

**Depends on:** None.

## Completion checklist

- [x] Use `UE58` and CMD builtins only.
- [x] Remove existing fixed plugin folders under `Engine/Plugins` and `Engine/Plugins/Marketplace` without prompting.
- [x] Reject missing or invalid engine configuration and report removal failures.
- [x] Exercise removal, repeat runs, and invalid configuration using disposable folders.
- [x] Document usage and ownership in the [Windows deployment component](../../architecture/windows-deployment-helper.md#engine-copy-removal).

This is support tooling only; runtime and companion versions are unchanged.
