---
feature_id: windows-deployment-commonui
status: completed
depends_on:
  - windows-deployment-install-modes
  - commonui-assets-inspect
released_in: null
release_track: support-tooling
---

# `windows-deployment-commonui` — CommonUI companion deployment option

**Outcome:** Windows users can independently select `UnrealMCPCommonUI` in the graphical deployment helper and install it with the compatible base plugin into a project or Engine.

**Implementation status:** Completed as support tooling without changing Unreal MCP, companion semantic, API, or extension-schema versions.

**Depends on:**

- [`windows-deployment-install-modes`](windows-deployment-install-modes.md)
- [`commonui-assets-inspect`](commonui-assets-inspect.md)

### Deployment behavior

- The CommonUI checkbox is off by default and independent from the GAS checkbox.
- Selecting CommonUI adds the fixed `UnrealMCPCommonUI` descriptor with the base descriptor as its UAT dependency.
- The base and every selected companion build before installation and share the existing stale-state, replacement, project-enablement, and rollback transaction.

### Verification

- Focused deployment tests cover the exact CommonUI descriptor/dependency command, selected destinations, project enablement, deterministic build order, and strict Boolean selection input.
- The full Python suite and documentation linter cover packaging metadata, indexed contracts, and unchanged runtime-version consistency.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
