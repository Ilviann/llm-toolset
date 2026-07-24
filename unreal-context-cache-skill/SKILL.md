---
name: unreal-context-cache
description: Maintain durable, indexed Markdown knowledge for workspaces containing Unreal Engine 5.8+ projects, including projects nested in Git repositories. Use when discovering or onboarding an Unreal workspace, reading project context, reconciling pulled or remote Git changes, implementing features, fixing bugs, reviewing architecture, or adding and changing Unreal C++, Blueprint, asset, or reflected data types. Discover every .uproject recursively, keep each project's docs and Git checkpoint separate, treat game-design documents as user-owned read-only input, and incrementally reconcile component and component-grouped type documentation.
---

# Unreal Context Cache

Keep verified project knowledge under each Unreal project's `docs/` directory. Treat those
documents as the canonical persistent context; do not create a second project-knowledge cache.

Before creating or changing documentation, read
[`references/documentation-schema.md`](references/documentation-schema.md) completely. When the
active project is inside a Git repository, also read
[`references/git-reconciliation.md`](references/git-reconciliation.md) completely.

## Discover projects

Run:

```powershell
python <skill-dir>/scripts/discover_unreal_projects.py <workspace-root>
```

Search recursively for `.uproject` files. Keep every project's documentation under that
project's own root, even when several projects share one workspace.

Interpret discovery status as follows:

- `supported`: maintain automatically.
- `unsupported`: do not initialize or maintain with this skill.
- `unknown`: do not infer the engine version. Ask the user to confirm that the project uses
  Unreal Engine 5.8 or newer before writing its documentation.

Custom engine association identifiers frequently do not contain a version. Never treat them as
proof of 5.8+.

## Establish the active project

Require the user to name the project for the current chat session. The user is responsible for
this selection.

- If one or more projects are found and none was named, list them and ask one concise question.
- Resolve the answer to an exact `.uproject` path.
- Do not perform project-specific implementation, diagnosis, or documentation updates until the
  active project is unambiguous.
- Continue to keep discovery and structural audit results separate for every found project.

## Initialize and audit documentation

For every supported or user-confirmed project, ensure this minimum structure exists without
overwriting existing content:

```text
docs/
├── index.md
├── reconciliation-state.md  # Required only when the project is in Git
├── gd/
│   └── index.md
├── components/
│   └── index.md
└── types/
    └── index.md
```

Create missing directories and minimal indexes when workspace writes are authorized. Preserve
existing documents and organization. Do not rename or delete documents merely to match a
preferred layout.

Audit a project with:

```powershell
python <skill-dir>/scripts/audit_docs.py <project-root>
```

For a project inside Git, require and validate its reconciliation state:

```powershell
python <skill-dir>/scripts/audit_docs.py <project-root> --require-git-state
```

Repair reported structural or inventory problems after inspecting the affected documents. Every
directory under `docs/` must have an `index.md` describing all immediate Markdown documents
except itself and all immediate subdirectories.

## Load context efficiently

Read `docs/index.md` first, then descend through only the indexes and documents relevant to the
task. Do not load the entire documentation tree by default.

Read relevant game-design documents before planning or implementing behavior. Treat their contents
as requirements and user-authored context, not as automatically correct descriptions of the
implementation.

Validate documentation claims against source code, configuration, asset metadata, editor output,
or other inspectable evidence. Clearly mark unknowns; never invent Blueprint or binary-asset
internals that cannot be inspected.

## Reconcile pulled Git changes incrementally

For an active project inside a Git repository, collect changes since its last successfully
reconciled source commit:

```powershell
python <skill-dir>/scripts/reconcile_git_changes.py <project-root>
```

Allow an explicit range:

```powershell
python <skill-dir>/scripts/reconcile_git_changes.py <project-root> --from <commit> --to <commit>
```

Treat the report as a navigation manifest, not documentation evidence by itself. Inspect relevant
diffs and current files, load only affected knowledge documents through their indexes, and
reconcile current durable knowledge.

- Review committed, staged, unstaged, and untracked changes separately.
- Review changed paths outside the configured tracked paths for possible shared plugin, module,
  configuration, or tooling dependencies.
- Use direct endpoint comparison from the documented source commit to the target source commit.
- Do not require a full project reindex when the checkpoint and changed evidence are usable.
- Do not infer semantic changes from binary `.uasset` or `.umap` diffs; perform targeted editor or
  asset-registry inspection when needed.

Advance an existing checkpoint only after relevant documentation is reconciled and the Git-aware
documentation audit passes:

```powershell
python <skill-dir>/scripts/update_reconciliation_state.py <project-root> --commit <target>
```

Add `--initialize` only after a one-time full reconciliation or after the user identifies a
trusted baseline. For initialization, run the normal audit first, create and index the state
document, then run the Git-aware audit. Add shared repository paths with
`--add-tracked-path <repo-relative-path>`.

Record the last reconciled **source** commit, not a documentation commit. Never advance the
checkpoint for uncommitted source changes. A later pass may see those changes after commit,
verify that documentation already covers them, and then advance without unnecessary edits.

## Protect game-design documents

Treat all files under `docs/gd/` as read-only unless the user explicitly requests
their creation or modification.

- A general request to implement a feature or fix a bug is not permission to edit a design task.
- Propose useful design-task updates in chat when appropriate.
- Do not save a proposal into that directory without explicit user authorization.
- When an authorized edit changes directory contents, update the affected indexes.

## Reconcile documentation after implementation

After implementing a feature or bug fix in the active project:

1. Inspect the actual changes, including Git-local changes when applicable, and the relevant
   existing documentation.
2. Reconcile `docs/components/` with changed responsibilities, boundaries,
   relationships, runtime flows, configuration, and important source locations.
3. Reconcile `docs/types/` with added, removed, renamed, or semantically changed types.
   Group type documentation by component directory.
4. Update every affected `index.md`, including ancestor indexes when children were added, removed,
   renamed, or re-described.
5. Run `audit_docs.py`, adding `--require-git-state` for a Git-managed project, and fix applicable
   errors.
6. Advance the Git checkpoint only when reconciling a stable committed source target.
7. Report which knowledge documents changed, or state that reconciliation found no knowledge
   change.

Reconciliation is mandatory after implementation work, but meaningless churn is not. If a fix
does not change durable architectural or data-type knowledge, verify the relevant documents and
leave them unchanged.

Use the same workflow when the user explicitly asks to refresh, repair, or audit project
knowledge without implementing code.

## Maintain evidence and scope

- Document durable project facts, decisions, invariants, and relationships—not transient task
  narration, raw command logs, exhaustive file listings, or build artifacts.
- Link claims to stable project-relative source paths where helpful.
- Keep one canonical home for each fact and cross-link instead of duplicating prose.
- For a type shared by several components, place it under its owning or primary component and
  cross-reference it from consumers.
- Never persist secrets, credentials, private keys, tokens, personal data, or machine-specific
  absolute paths.
- Never derive documentation from `Binaries/`, `DerivedDataCache/`, `Intermediate/`, or `Saved/`
  except when a specific diagnostic task requires temporary inspection.
- Preserve user-authored nuance and unrelated worktree changes.
