---
feature_id: windows-deployment-install-modes
status: completed
depends_on:
  - companion-plugins
released_in: "0.32.0"
---

# `windows-deployment-install-modes` — Windows deployment companion and install modes

**Outcome:** Windows users can build and install the base plugin alone or with `UnrealMCPGAS`, targeting the selected project or Engine with explicit enablement policy.

**Implementation status:** Completed in 0.32.0 on unchanged companion API v1 and extension-schema revision 1. The independently versioned GAS companion remains 0.2.0.

**Depends on:**

- [`companion-plugins`](companion-plugins.md)

### Deployment behavior

- The base plugin is always built. The optional GAS checkbox adds one companion build with the fixed base descriptor as its UAT dependency.
- Project mode installs selected plugins below the project `Plugins` directory and atomically adds or enables their exact `.uproject` references.
- Engine modes install below the selected Engine's `Engine/Plugins/Marketplace` directory and set every installed descriptor's `EnabledByDefault` value to the selected Boolean state.
- Every selected package builds and verifies before installation. Staging, replacement, final verification, and project enablement form one rollback-safe selected-plugin transaction; destination or project-descriptor changes during the build reject as stale.
- Existing replacement, matching-PDB deployment, readonly/writable server access, and optional editor lifecycle remain explicit and independent.

### Verification

- Python deployment tests cover fixed base/GAS build commands, all destinations and enablement states, bounded descriptor mutation, invalid inputs, per-plugin binary filtering, pair installation, and restoration after a final enablement failure.
- The full Python suite and documentation linter cover release consistency and indexed contracts. Windows UAT qualification builds the base and GAS packages against the selected supported Engine.
- The deployment helper changes no runtime extension interface, schema, bridge command, or native plugin behavior.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
