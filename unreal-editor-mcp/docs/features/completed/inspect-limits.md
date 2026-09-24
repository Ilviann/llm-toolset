---
feature_id: inspect-limits
status: completed
depends_on:
  - asset-inspect-core
  - asset-inspect-animation
released_in: "0.53.1"
---
# Independent inspection budgets

**Depends on:**

- [`asset-inspect-core`](asset-inspect-core.md)
- [`asset-inspect-animation`](asset-inspect-animation.md)

## Contract

Separate the internal Blueprint inspector's inclusive 4,096 emitted-record ceiling from 262,144 graph-work units and 262,144 fingerprint entries. Graph selection affects output only; whole-scope snapshots invalidate selected cursors after unrelated graph changes. Animation structural processing uses the work budget without changing semantic output or companion API v2.

## Completion gates

- [x] Independent collection, companion, pre-pagination, and capability checks.
- [x] Result/fingerprint boundaries, bounded traversal, selected cursors, and large Animation Blueprint regressions.
- [x] Focused native regressions and full Python suite (205 tests, one Windows symlink-permission skip).
- [x] Windows UE 5.8 adaptive, forced-unity (`-ForceUnity -DisableAdaptiveUnity`), and non-unity builds.
- [x] Full Unreal Automation (74 successful test executions; 69 required case names).
- [x] Full headless production-socket integration.
- [x] Dedicated animation restart.
- [x] Win64 base packaging and documentation lint.

## Validation scope

Validated on Windows with this checkout's UE 5.8 installation. Win64 packaging produced version 0.53.1 with companion API 2; no companion versions changed. The full Python suite ran 205 tests, with one symlink test skipped because Windows symlink creation was unavailable. macOS native verification remains a preferred follow-up in the roadmap; Linux is outside this branch's support scope.

[Architecture](../../architecture/blueprint-inspector.md) · [Roadmap](../../../ROADMAP.md)
