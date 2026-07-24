# Git reconciliation

Use incremental Git evidence to reconcile documentation after remote or pulled changes without
rescanning the complete Unreal project.

## Contents

- [State document](#state-document)
- [Tracked paths](#tracked-paths)
- [Collection workflow](#collection-workflow)
- [Reconciliation workflow](#reconciliation-workflow)
- [Checkpoint rules](#checkpoint-rules)
- [History and branch cases](#history-and-branch-cases)
- [Binary assets](#binary-assets)
- [When full reconciliation is necessary](#when-full-reconciliation-is-necessary)

## State document

Keep one checkpoint per Unreal project at `docs/reconciliation-state.md`. Use this exact marker:

```markdown
# Documentation reconciliation state

<!-- unreal-context-cache-state
schema: 1
last-reconciled-source-commit: 0123456789abcdef0123456789abcdef01234567
tracked-path: MyGame
tracked-path: Plugins/SharedGameplay
-->

The checkpoint identifies the committed source tree against which this project's documentation
was last reconciled. Tracked paths are relative to the Git repository root.
```

Use a full Git object ID. The state is machine-readable but remains inside a Markdown document.
List the document in `docs/index.md`.

The checkpoint names a source commit, not the commit that contains the documentation update.
This avoids a self-referencing commit cycle.

## Tracked paths

Always include the project directory relative to the repository root. Use `.` when the Unreal
project is at the repository root.

Add repository-relative paths for shared dependencies that can change project knowledge:

- Shared plugins or modules
- Shared configuration
- Build, packaging, code-generation, or editor tooling
- Common content or schemas

Reject absolute paths and paths containing `..`.

The collector also reports committed paths outside the tracked scope. Review that list for newly
introduced shared dependencies. Add a path to the state only when it can affect the project.

## Collection workflow

Run from any directory:

```powershell
python <skill-dir>/scripts/reconcile_git_changes.py <project-root>
```

The default base comes from `last-reconciled-source-commit`; the default target is `HEAD`.

Override either endpoint when the user supplies a range:

```powershell
python <skill-dir>/scripts/reconcile_git_changes.py <project-root> --from <base> --to <target>
```

The collector resolves both endpoints to full commit IDs and compares their trees directly. It
reports:

- Committed changes within tracked paths
- Committed changes outside tracked paths
- Staged changes within tracked paths
- Unstaged changes within tracked paths
- Untracked files within tracked paths
- Path classifications and warnings

Use `--json` for machine-readable output.

The report is a candidate manifest. A changed filename or commit message is not sufficient
evidence for a documentation claim.

## Reconciliation workflow

1. Capture the collector's resolved target before editing documentation.
2. Review changed paths outside tracked scope and add newly relevant shared paths.
3. Map relevant changed files to architectural components and durable data types.
4. Read the affected documentation indexes, then the affected documents.
5. Inspect focused source diffs and current files. Prefer current endpoint state over narrating
   intermediate commits.
6. Inspect staged, unstaged, and untracked changes separately; they are not part of the committed
   target.
7. Reconcile architecture and data-type documents.
8. Preserve remote documentation edits and resolve conflicts from evidence.
9. Update all affected indexes.
10. Run the Git-aware documentation audit.
11. Advance the checkpoint only when the checkpoint rules allow it.

Net endpoint comparison is appropriate for current-state documentation: a change introduced and
then reverted inside the range normally requires no durable documentation update.

## Checkpoint rules

Advance with:

```powershell
python <skill-dir>/scripts/update_reconciliation_state.py <project-root> --commit <target>
```

Run this only after:

- All relevant committed changes through the target were inspected.
- Architecture and data-type documentation was reconciled.
- Binary changes received the necessary targeted inspection.
- `audit_docs.py --require-git-state` passed.

Do not advance for staged, unstaged, or untracked source changes. After those changes are
committed, a later run can confirm that the documentation already covers them and advance without
rewriting it.

Initialize with:

```powershell
python <skill-dir>/scripts/update_reconciliation_state.py <project-root> --commit <target> --initialize
```

Initialize only after a one-time full reconciliation or when the user explicitly identifies a
trusted documented baseline. Before initialization, run the normal documentation audit. After
creating and indexing the state document, run `audit_docs.py --require-git-state`.

Add shared paths while initializing or updating:

```powershell
python <skill-dir>/scripts/update_reconciliation_state.py <project-root> `
  --commit <target> `
  --add-tracked-path Plugins/SharedGameplay
```

## History and branch cases

If the base is not an ancestor of the target but both commits exist, compare the base tree
directly with the target tree and warn about the branch change. The documentation describes the
base snapshot, so direct endpoint comparison captures additions, removals, and replacements
needed for the target snapshot.

If the base object is unavailable after a shallow clone, garbage collection, or rewritten
history:

1. Fetch the missing history when authorized and practical.
2. Otherwise ask the user for a known replacement baseline.
3. Fall back to a targeted or full reconciliation if no trustworthy baseline is available.

Do not silently substitute a merge base; it may omit facts that existed in the documented base
but no longer exist in the target.

## Binary assets

Git identifies changed `.uasset` and `.umap` paths but usually cannot provide their semantics.
Use targeted Unreal Editor output, asset-registry data, exported text, source references, or
user-provided evidence.

Do not update detailed type or architecture claims from a binary filename alone. Record an
unknown or request inspection when the necessary evidence is unavailable.

## When full reconciliation is necessary

Avoid a full project scan unless at least one condition applies:

- No trustworthy initial checkpoint exists.
- The checkpoint object and a replacement baseline are unavailable.
- Existing documentation is demonstrably unreliable or structurally incomplete.
- A large project reorganization defeats path-to-component mapping.
- The user explicitly requests a complete reconciliation.

A missing documentation update in one component is not by itself evidence that the entire project
must be reindexed.
