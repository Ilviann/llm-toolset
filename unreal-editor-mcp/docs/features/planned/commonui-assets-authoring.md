---
feature_id: commonui-assets-authoring
status: planned
depends_on:
  - commonui-assets-inspect
  - asset-authoring-kernel
  - companion-asset-adapters
released_in: null
---

# `commonui-assets-authoring` — Create/update assets with CommonUI plugin dependencies

**Outcome:** Agents can create and stale-safely update supported CommonUI-dependent assets through the optional `UnrealMCPCommonUI` companion after bounded inspection is available.

**Depends on:**

- [`commonui-assets-inspect`](../completed/commonui-assets-inspect.md)
- [`asset-authoring-kernel`](../completed/asset-authoring-kernel.md)
- [`companion-asset-adapters`](../completed/companion-asset-adapters.md)

**Planning note:** Before implementation, freeze creation and mutation operations separately for each inspected asset family against the supported Unreal Engine build and public CommonUI editor APIs. Inspection support does not imply that a family is safe to create or update.

### Creation and update contract

- Reuse the independently versioned `UnrealMCPCommonUI` companion, effective project-enablement checks, strict companion API v2 admission, shared authenticated bridge, Game-thread dispatch, operation ledger, errors, limits, packaging, and snapshots established by `commonui-assets-inspect` and `companion-asset-adapters`.
- Add mutation capability only in explicit writable mode and only for inspected families whose public editor APIs support bounded creation or updating with exact read-back. Never install or enable CommonUI, edit the project descriptor, add CommonUI dependencies to `UnrealMCP`, or add a separate model-facing tool.
- Reuse existing asset, Blueprint, Widget-tree, default, member, graph, compile, and save operations where their released contracts apply. Add only typed CommonUI-owned operation discriminators required by the frozen family allowlist; do not expose unrestricted reflection, arbitrary UObject calls, runtime widget execution, or opaque bulk replacement.
- Require current inspection snapshots and stable nested identities for updates. Prevalidate parents, packages, types, references, limits, and plugin readiness; preserve unrelated content; use one retained transaction per accepted mutation where Unreal supports it; verify exact postconditions and restore state on unexpected failure.
- Keep unsupported CommonUI families, project-wide CommonUI settings, input-device simulation, runtime activation or navigation, PIE interaction, plugin installation or enablement, and arbitrary dependency-graph rewriting outside this feature unless a later roadmap feature names them.

### Verification and completion gate

- Create and update representative assets for every frozen authoring family, compile and save where applicable, restart, and verify exact read-back through `commonui-assets-inspect`.
- Test invalid parents, packages, classes, references, values, stale snapshots and identities, disabled or lost plugin state, inspection-only and mismatched companions, compile/save failure, limits, timeout, replay, lost-response recovery, Undo/Redo, and restoration without partial mutation registration or unrelated changes.
- Run focused native Automation, normal, forced-unity, and non-unity builds, packaging, release-contract, and cross-process writable verification on Windows; record macOS as preferred non-blocking follow-up.
- Document supported creation and update workflows, writable-mode and snapshot requirements, compilation and saving, limits, exclusions, recovery, and focused examples. Complete the feature only when every supported family can be created or updated, saved, restarted, and read back without altering unrelated content, unavailable states expose no mutations, and the complete base and CommonUI companion suites pass on Windows.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
