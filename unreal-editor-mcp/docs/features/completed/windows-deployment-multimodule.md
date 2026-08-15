---
feature_id: windows-deployment-multimodule
status: completed
depends_on:
  - native-domain-modules
  - python-tooling-decomposition
released_in: null
release_track: support-tooling
---

# `windows-deployment-multimodule` — Multi-module binary deployment

**Outcome:** Windows binary deployment strips implementation source and retains usable precompiled rules for every module declared by each selected plugin.

**Implementation status:** Completed as support tooling without changing Unreal MCP, companion semantic, API, or extension-schema versions.

**Depends on:**

- [`native-domain-modules`](native-domain-modules.md)
- [`python-tooling-decomposition`](python-tooling-decomposition.md)

### Deployment behavior

- Before staging, the helper reads the restored installed descriptor and admits a non-empty bounded list of unique, path-safe module names.
- The copy filter retains only each declared module's matching `Build.cs` file from its source tree and removes implementation and headers from module trees and UAT-generated package intermediates.
- Configuration and final verification require every declared module rule and add or confirm `bUsePrecompiled = true` for each one. Removing or allowlisting one implementation file cannot bypass the complete source-free binary deployment gate.

### Verification

- Focused deployment tests reproduce the base plugin's `UnrealMCP` plus `UnrealMCPUMG` layout, exact `UnrealMCPUMGInspectionAdapter.cpp` failure path, and generated intermediate source, then verify both module rules and the absence of implementation source.
- Invalid empty, traversal-like, duplicate, and over-limit module lists fail before installation.
- A UE 5.8 Win64 UAT package of the six-module base plugin passes the real binary-install filter with every declared module rule configured and no implementation source retained.
- The full Python suite and documentation linter cover unchanged runtime contracts and indexed support-tooling documentation.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
