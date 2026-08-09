---
feature_id: windows-deployment-validation-fixes
status: completed
depends_on:
  - windows-deployment-commonui
released_in: null
release_track: support-tooling
---

# `windows-deployment-validation-fixes` — Deployment selection and Engine validation fixes

**Outcome:** Windows deployment can install the base, GAS, and CommonUI plugins in one transaction, and every packaging entry path rejects Unreal Engine versions older than 5.8.

**Implementation status:** Completed as support tooling without changing Unreal MCP, companion semantic, API, or extension-schema versions.

**Depends on:**

- [`windows-deployment-commonui`](windows-deployment-commonui.md)

### Deployment behavior

- The selected-plugin transaction accepts the complete fixed set of the base plugin plus both optional companions while retaining unique-name validation, all-package staging, final verification, and rollback.
- The packaging helper owns bounded `Build.version` parsing and the Unreal Engine 5.8-or-newer check. Direct packaging and graphical Windows deployment use that same validator.

### Verification

- Focused packaging tests cover valid platform launchers and rejection of unsupported or malformed Engine version metadata.
- Focused deployment tests cover successful three-plugin Engine installation and restoration of all three prior installations when final project enablement fails.
- The full Python suite and documentation linter cover unchanged runtime contracts and indexed support-tooling documentation.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
