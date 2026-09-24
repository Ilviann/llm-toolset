---
feature_id: windows-deployment-ai
status: completed
depends_on:
  - windows-deployment-enhanced-input-preview
  - ai-assets-inspect
released_in: null
release_track: support-tooling
---

# `windows-deployment-ai` — AI companion deployment

**Outcome:** The Windows deployment helper offers every implemented production companion, including `UnrealMCPAI`, through independent default-off checkboxes.

**Depends on:**

- [`windows-deployment-enhanced-input-preview`](windows-deployment-enhanced-input-preview.md)
- [`ai-assets-inspect`](ai-assets-inspect.md)

## Behavior

- AI uses its fixed repository descriptor and the base plugin dependency after GAS, CommonUI, and Enhanced Input in deterministic build order.
- AI participates in project enablement and both Engine default-enablement modes, replacement checks, staging, verification, and rollback.
- The transaction admits at most five plugins: the base plus four production companions. The disposable test companion remains excluded from graphical deployment.
- This support-tooling change does not alter runtime behavior, companion APIs, or semantic versions.

## Verification

- Deployment tests compare offered plugins with repository production descriptors, validate every companion build command and all 16 selection combinations, and reject invalid AI flags and oversized transactions.
- Temporary-package tests cover AI alone and the complete set in all three install modes, plus rollback of the complete set after project enablement fails.
- Run the full Python suite and documentation linter. Native packaging is unchanged; these checks do not build or install real plugins.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
