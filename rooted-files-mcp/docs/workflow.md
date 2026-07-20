# Feature implementation workflow

Use this workflow for every Rooted Files MCP feature, fix, or refactor.

## 1. Build the working set

1. Start at [`index.md`](index.md).
2. Read the owning page in [`architecture/`](architecture/index.md) to identify component boundaries, direct dependencies, security invariants, and verification.
3. Read only the affected component folders in [`types/`](types/index.md), beginning with their local `index.md` files.
4. Inspect the named source, tests, package metadata, README examples, and roadmap entries. Expand the working set only when source evidence reveals another dependency.

Do not infer current behavior from prose alone. Tool schemas, executable constants, package metadata, and behavioral tests are the authoritative implementation contracts.

## 2. Design the change

- Assign every new responsibility to one component. Add one architecture file if a genuinely new component is required.
- Trace security policy through both configuration and filesystem enforcement: permissions, root confinement, requested/resolved hidden paths, protected names, binary/text classification, size bounds, and pre-replacement revalidation.
- Keep model-facing paths root-relative, schemas compact, errors stable, and operations bounded.
- Prefer narrow standard-library collaborators. Do not introduce a cloud service, account, telemetry, runtime download, or dependency without explicit authorization.
- Record useful out-of-scope refactoring separately rather than expanding the requested change.

## 3. Implement and verify

- Change the smallest coherent source set.
- Add normal, invalid-input, resource-limit, security-boundary, and platform-branch coverage proportional to the change.
- Run focused tests while iterating, then the complete application suite after behavior changes.
- Preserve defense in depth: permission-disabled tools are both omitted from the catalog and rejected by the filesystem service.
- Preserve protocol-only stdout; route diagnostics to stderr.

## 4. Update the knowledge cache

Update documentation in the same change as implementation:

- Ownership, dependencies, invariants, known pressure, or verification changed: update the owning `architecture/*.md` file.
- A custom type, policy object, record, or reusable function library changed: update its file under `types/<component>/`.
- A new documentation file or folder was added: update only its immediate parent `index.md`.
- User-visible installation, configuration, tools, or behavior changed: update `README.md`.
- Planned scope changed: update `ROADMAP.md` as required.

Every directory under `docs/` must contain an `index.md`. Each index describes only its immediate files and subdirectories. Keep links relative and never recreate the removed `CODE.md` monolith.

## 5. Release consistency

When a roadmap phase changes behavior, update the version according to repository rules and keep `rooted_files_mcp/__init__.py`, `pyproject.toml`, initialization results, tests, README, and release history synchronized where present. Documentation is explanatory and must not be used as executable release input or a test fixture.
