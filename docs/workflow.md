# Workspace workflow

Use this workflow for changes anywhere in the workspace. Project `AGENTS.md` and `docs/workflow.md` files may add project-specific rules but must not reference this document.

## Section-addressable documentation

Before implementation work, verify that at least one available tool can retrieve a selected Markdown section by heading or fragment without loading the whole document. If no such tool is available, warn the user and stop working. Resume only after the tool becomes available or the user explicitly asks to continue without it.

## Long-running feature design

For a feature design that may span sessions or use sub-agents, maintain incremental working notes under the affected application's `docs/plans/` directory. Notes may be created and updated before requirements gathering is complete so decisions survive context-window limits and bounded research tasks can be handed off without relying on conversation history.

Clearly distinguish confirmed decisions, proposals, open questions, rejected alternatives, and verification evidence. Keep each plan's immediate index current. Treat these notes as design workspace rather than executable truth; when the design is accepted, move its durable feature contract into the applicable status directory under `docs/features/` and update affected architecture or type documentation through the normal change workflow.

## Change workflow

Use this workflow for every feature, fix, or refactor.

1. Start at the applicable `docs/index.md`: use the workspace index for workspace-wide changes and the affected application's index for application changes. Read the owning architecture page, affected type indexes, and relevant feature document when one exists, then inspect the relevant source, tests, metadata, examples, history, roadmap, and user documentation. Expand the working set only when evidence reveals another dependency.
2. Assign each new responsibility to one component. Identify the affected runtime or process boundaries, ownership, data and command flow, errors, limits, versions, state, persistence, platform behavior, and security invariants required by the change.
3. Make the smallest coherent change. Add proportional normal, invalid, limit, security, and platform coverage; run focused tests, then the full affected application suite after behavior changes.
4. Update changed architecture and type references and their immediate indexes. Update `README.md` for user-visible changes and `ROADMAP.md` or `HISTORY.md`, when present, for scope or release changes. After Markdown changes, run `python internal/tools/lint_docs.py` from the repository root.

## Runtime and support-tooling releases

Classify a change as support tooling only when it affects repository scripts, deployment helpers, documentation infrastructure, or development utilities without changing an application's runtime, exposed capabilities, companion or plugin APIs, or project-content behavior. If a change affects both support tooling and runtime behavior or contracts, classify it as runtime work.

Support-tooling-only changes do not trigger or require an application version change. Track them in the affected application's separate **Support tooling** roadmap section, set feature front matter to `release_track: support-tooling`, and retain `released_in: null` after completion. Runtime features may omit `release_track` or set it to `runtime`.

For completed runtime work in a versioned application, update the application version after each feature: patch for fixes, minor for features or improvements, and major only when requested. Synchronize all application-owned version sources and history.
