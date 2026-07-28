---
name: maintain-project-documentation
description: Keep implementation documentation aligned with a software repository. Use when adopting this documentation convention, maintaining docs during feature or fix work, repairing indexes or cache markers, or explicitly reconciling docs after external Git changes.
---

# Maintain Project Documentation

Obey repository-local instructions and preserve unrelated changes.

## Rules

- Document verified final behavior, not plans or assumptions.
- Read relevant implementation docs before changing a feature; after verification,
  update only docs affected by the task.
- Store architectural-component responsibilities, boundaries, relationships, and
  runtime behavior under `<docs-root>/components/`.
- Store durable semantics for important classes, structures, enums, interfaces,
  schemas, and assets under `<docs-root>/types/`, grouped by owning component.
- In each managed docs tree, require every directory's `index.md` to list its
  immediate files and subdirectories, excluding `index.md` and `.cache.md`.
- Require each docs root's `.cache.md` to record an ISO 8601 timestamp and Git
  branch. If that tree changes, update the cache exactly once per conversation.
- Respect locally protected, generated, vendored, or design documentation.
- Report material complexity, coupling, or ownership problems and recommend a
  refactor without expanding scope.

## External changes

Never reconcile documentation merely because a pull, merge, unfamiliar commit, or
stale content is observed. Do it only on explicit user request.

For each relevant docs root:

1. Find the latest commit that changed its cache:
   `git log -1 --format=%H -- <docs-root>/.cache.md`
2. Inspect changes since that baseline with `git diff --name-status <baseline>` and
   `git status --short`.
3. Verify current behavior from source, tests, configuration, schemas, or assets;
   do not rely only on diffs or commit messages.
4. Reconcile affected docs and indexes, then update the cache once.

If no cache commit exists, request a baseline instead of inventing one. Use a
separate baseline for each docs root.

## Adoption

If the project has no conflicting convention, create the smallest useful tree:
`docs/index.md`, `docs/.cache.md`, and a concise workflow file. Add deeper
feature/type structure only when justified, and link the workflow from brief agent
instructions when the project uses them.
