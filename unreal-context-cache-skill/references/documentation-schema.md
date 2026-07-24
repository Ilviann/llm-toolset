# Documentation schema

Use this schema when initializing, reading, auditing, or updating a project's `docs/` tree.

## Contents

- [Canonical directories](#canonical-directories)
- [Git documentation marker](#git-documentation-marker)
- [Index contract](#index-contract)
- [Game-design documents](#game-design-documents)
- [Architectural components](#architectural-components)
- [Data types](#data-types)
- [Writing and evidence rules](#writing-and-evidence-rules)

## Canonical directories

Use these exact top-level names:

| Path | Ownership | Purpose |
| --- | --- | --- |
| `docs/gd/` | User-owned | Desired behavior, constraints, acceptance criteria, and design intent |
| `docs/components/` | Agent-maintained | Component responsibilities, boundaries, relationships, and runtime behavior |
| `docs/types/` | Agent-maintained | Durable type semantics, grouped into component directories |

Other concepts may be added in later versions. Do not invent additional top-level concepts just
to store temporary notes. Place durable knowledge in the closest existing concept or ask the user
when none fits.

## Git documentation marker

For a project inside a Git repository, maintain `docs/.cache.md` as operational Markdown metadata.
Do not list it in `docs/index.md`.

Refresh the file with the current time and branch name after documentation reconciliation and
before the documentation commit. Git history of this file identifies the latest confirmed
documentation commit. It does not replace component or type documentation.

Read [`git-reconciliation.md`](git-reconciliation.md) for the complete workflow. Do not create the
marker outside Git or refresh it before documentation validation succeeds.

## Index contract

Name every directory inventory `index.md`.

An index inventories its immediate children only:

- List every Markdown document in the directory except `index.md`.
- List every immediate subdirectory.
- Give every entry a brief, meaningful description.
- Use relative Markdown links.
- Keep descriptions synchronized when a document or directory changes purpose.
- Do not list generated, transient, or hidden filesystem entries.

Treat `docs/.cache.md` as the sole Markdown exception to the document inventory. Validate it
separately for Git-managed projects and never list it in `docs/index.md`.

Use this shape:

```markdown
# <Directory title>

<One-sentence purpose of this directory.>

## Documents

- [document-name.md](document-name.md) — Brief description.

## Subfolders

- [component-name/](component-name/index.md) — Brief description.
```

Use `None.` below an empty `Documents` or `Subfolders` heading. Omit neither heading. An index does
not list itself, avoiding a circular self-entry.

The project-level `docs/index.md` should describe the documentation set and link all three
canonical top-level directories.

## Game-design documents

Allow the user to choose organization and document format below `gd/`, subject to
the index contract. Read relevant tasks as requirements before implementation.

Do not silently transform design intent into implementation claims. If code and design disagree,
report the discrepancy.

Only create or change these documents when the user explicitly asks. An explicit request can
authorize a precise file edit or a clearly scoped design-document update; do not expand it to
unrelated tasks.

## Architectural components

Prefer one component document per coherent subsystem:

```text
components/
├── index.md
├── ability-system.md
└── inventory.md
```

Use a component subdirectory when the material would otherwise become difficult to navigate.
Every such subdirectory requires its own `index.md`.

Use the applicable sections:

```markdown
# <Component name>

## Purpose
## Responsibilities
## Boundaries and invariants
## Collaborators
## Runtime flow
## Configuration
## Important source locations
## Extension points
```

Do not create empty sections. Describe externally meaningful architecture, not line-by-line
implementation.

Recognize Unreal components at the level appropriate to the project: modules and plugins,
gameplay subsystems, actors and actor components, gameplay framework classes, systems built from
Blueprints or assets, services, save/load, networking, UI, AI, and build or editor tooling.

## Data types

Group types by their owning architectural component:

```text
types/
├── index.md
├── ability-system/
│   ├── index.md
│   └── gameplay-effect-context.md
└── inventory/
    ├── index.md
    ├── item-definition.md
    └── item-instance.md
```

The component directory description should link to the corresponding architecture document.
When ownership is shared, choose one primary owner and add cross-links from consuming components.

Document types whose semantics matter across tasks, including applicable C++ and Blueprint
classes, `USTRUCT` and `UENUM` definitions, interfaces, delegates, configuration structures, save
or network payloads, data assets, data-table row structures, primary asset types, and important
identifiers.

Use the applicable sections:

```markdown
# <Type or related type group>

- **Kind:** Class, struct, enum, interface, asset, row type, or other
- **Owning component:** Link to component documentation
- **Defined at:** Stable project-relative source or asset path

## Purpose
## Fields and semantics
## Lifecycle and ownership
## Serialization, replication, and versioning
## Producers and consumers
## Invariants
```

Do not dump every field mechanically. Capture durable meaning, constraints, defaults, units,
ownership, compatibility, serialization, and replication behavior that future work needs.

Binary `.uasset` contents are not self-evident. Use verified editor output, asset-registry data,
exported text, source references, or user-provided information. Otherwise mark the detail
unknown.

## Writing and evidence rules

- Write all persistent knowledge as Markdown under the owning project's `docs/`.
- Use project-relative paths with `/` separators.
- Prefer present tense and concise factual prose.
- Separate verified current behavior from proposed behavior.
- Record durable rationale when evidence exists; do not reconstruct intent from guesswork.
- Cross-link canonical documents rather than copying the same explanation.
- Update indexes in the same change as their children.
- Avoid timestamps or “last updated” fields unless the project explicitly requires them; version
  control already records change time.
