# Git reconciliation

Use `docs/.cache.md` history to reconcile documentation after remote or pulled changes without
rescanning the complete Unreal project.

## Contents

- [Cache marker](#cache-marker)
- [Find the confirmed commit](#find-the-confirmed-commit)
- [Collect changes](#collect-changes)
- [Reconcile documentation](#reconcile-documentation)
- [Refresh the marker](#refresh-the-marker)
- [History and branch cases](#history-and-branch-cases)
- [Binary assets](#binary-assets)
- [When full reconciliation is necessary](#when-full-reconciliation-is-necessary)

## Cache marker

Keep one operational marker per Git-managed Unreal project at `docs/.cache.md`:

```markdown
# Documentation cache

- Updated: `2026-07-24T16:30:00.123456+03:00`
- Branch: `feature/inventory`
```

Do not list this hidden marker in `docs/index.md`. It is the only Markdown file exempt from the
index inventory.

The timestamp and branch name are informational. The Git commit that contains the marker change
is the authoritative documentation confirmation.

## Find the confirmed commit

From the repository root, find the latest marker commit reachable from the target:

```powershell
git log -1 --format=%H <target> -- <project-path>/docs/.cache.md
```

Use `HEAD` as the default target. Compare that commit directly with the target tree.

If marker history does not exist, require an explicit `--from` commit or perform a one-time full
reconciliation, refresh the marker, and commit it.

## Collect changes

Run:

```powershell
python <skill-dir>/scripts/reconcile_git_changes.py <project-root>
```

The default base is the latest marker commit. The default target is `HEAD`.

Override the range:

```powershell
python <skill-dir>/scripts/reconcile_git_changes.py <project-root> --from <base> --to <target>
```

Temporarily include shared repository paths:

```powershell
python <skill-dir>/scripts/reconcile_git_changes.py <project-root> `
  --add-tracked-path Plugins/SharedGameplay
```

The collector reports:

- Committed changes inside the project and additional tracked paths
- Committed changes outside that scope
- Staged, unstaged, and untracked changes inside scope
- Path classifications and warnings

Use `--json` for machine-readable output. Treat the report as navigation, not sufficient evidence
for documentation claims.

## Reconcile documentation

1. Capture the resolved base and target.
2. Review outside-scope changes for shared dependencies.
3. Map relevant files to components and durable types.
4. Read affected indexes and documents.
5. Inspect focused diffs and current endpoint files.
6. Reconcile `docs/components/` and `docs/types/`.
7. Preserve remote documentation edits.
8. Update affected indexes.
9. Run `audit_docs.py --require-git-cache`.
10. Refresh the marker.

Net endpoint comparison is appropriate for current-state documentation. A change introduced and
reverted inside the range normally requires no durable documentation update.

## Refresh the marker

After successful documentation reconciliation and audit, run:

```powershell
python <skill-dir>/scripts/mark_docs_cache.py <project-root>
```

Run this before the user commits documentation so the confirmation marker and the knowledge
changes share one commit.

Do not refresh the marker merely because arbitrary Markdown changed. Refresh it only when the
project's durable documentation is confirmed against the inspected source state.

If two branches change the marker and conflict during merge, resolve the documentation first and
refresh the marker with the merge branch and current time.

## History and branch cases

The branch stored in the marker is informational because branches can be renamed, merged, or
deleted. Always use the marker's Git history for the base commit.

If the marker commit is not an ancestor of the target but both commits exist, compare their trees
directly and review removals and replacements carefully.

If marker history is unavailable after a shallow clone or rewritten history:

1. Fetch the missing history when authorized and practical.
2. Otherwise ask for an explicit replacement base.
3. Fall back to a targeted or full reconciliation if no trustworthy base exists.

Do not silently substitute a merge base; it may omit facts present in the previously confirmed
documentation snapshot.

## Binary assets

Git identifies changed `.uasset` and `.umap` paths but usually cannot provide semantics. Use
targeted Unreal Editor output, asset-registry data, exported text, source references, or
user-provided evidence.

Do not update detailed type or component claims from a binary filename alone.

## When full reconciliation is necessary

Avoid a full project scan unless:

- No marker history or trustworthy explicit base exists.
- Existing documentation is demonstrably unreliable or structurally incomplete.
- A large reorganization defeats path-to-component mapping.
- The user explicitly requests complete reconciliation.

A missing update in one component does not by itself require full reindexing.
