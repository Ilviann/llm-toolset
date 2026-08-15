# Issue records

This directory tracks confirmed errors through investigation and resolution. Each error has one numbered `issue-#.md` document; new documents use the next integer and existing numbers are never reused.

## Open issues

- [`issue-4.md`](issue-4.md) — macOS lifecycle-only acceptance remains blocked because the committed entrypoint is Windows-only and the direct helper's editor launch exits before authenticated bridge readiness.
- [`issue-3.md`](issue-3.md) — `blueprint_action_catalog` consistently rejects the unchanged snapshot returned by `blueprint_inspect` for a native-parent Actor Blueprint on Unreal Engine 5.8.0.
- [`issue-2.md`](issue-2.md) — Unreal Editor can intermittently crash during an `editor_lifecycle` restart; investigation is awaiting crash logs and reproduction details.

## Resolved issues

- [`issue-1.md`](issue-1.md) — UE 5.8 regenerated one inert hidden promotable-operator tolerance-pin GUID on first reload; 0.17.1 canonicalizes that non-structural pin out of model-facing identities and snapshots.
