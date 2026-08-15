---
feature_id: xcode-app-environment
status: completed
depends_on:
  - python-tooling-decomposition
released_in: null
release_track: support-tooling
---

# `xcode-app-environment` — Pinned Xcode application environment

**Outcome:** macOS development tools select Xcode 26.1.1 from one `XCODE26_1_1` application path and derive Apple's required developer directory themselves.

**Implementation status:** Completed as support tooling. It does not change Unreal MCP or companion versions, runtime behavior, schemas, or capabilities.

**Depends on:**

- [`python-tooling-decomposition`](python-tooling-decomposition.md)

## Contract

- `XCODE26_1_1` points to `Xcode.app`, not its `Contents/Developer` child.
- Packaging and headless integration append `Contents/Developer`, require `usr/bin/xcodebuild`, and export the resolved result as `DEVELOPER_DIR` to macOS child processes.
- An inherited `DEVELOPER_DIR` or the retired `UNREAL_MCP_DEVELOPER_DIR` cannot replace the pinned application path.
- Packaging retains `--developer-dir` as an explicit direct `Contents/Developer` override.
- Windows and Linux support-tool environments do not require or derive Xcode paths.

## Verification

- Cover application-root derivation, `xcodebuild` validation, inherited-environment replacement, retired-variable rejection, explicit packaging override behavior, and non-macOS branches in the focused packaging and headless tests.
- Run the complete Python suite and the documentation linter. Native compilation and packaging are not required because UAT arguments, plugin source, runtime behavior, and output verification are unchanged.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
