# Feature implementation workflow

Use this workflow for every Godot Editor MCP feature, fix, or refactor.

## 1. Build the working set

1. Start at [`index.md`](index.md).
2. Read the relevant page in [`architecture/`](architecture/index.md) to identify the owning component, direct dependencies, cross-language contracts, and verification surface.
3. Read only the affected component folders in [`types/`](types/index.md). Follow their local `index.md` files before opening individual references.
4. Inspect the named source, metadata, examples, tests, history, and roadmap entries. Expand the working set only when source evidence reveals another dependency.

Do not infer current behavior from prose alone. Schemas, executable constants, package/plugin metadata, handler registrations, and behavioral tests are the authoritative implementation contracts.

## 2. Design the change

- Assign every new responsibility to one component. If it needs a new component, add one architecture file and link it from `architecture/index.md`.
- Identify every boundary the change crosses: Python/Godot wire data, command ownership, errors, limits, versions, persistent records, waits, and runtime debugger identities.
- Keep model-facing schemas bounded and validate on both sides of the bridge where applicable.
- Prefer narrow collaborators over adding dependencies to unrelated services.
- Record useful out-of-scope refactoring separately; do not combine it with the feature without authorization.

## 3. Implement and verify

- Change the smallest coherent source set.
- Add normal, invalid-input, resource-limit, security-boundary, and platform-branch tests proportional to the change.
- Run focused tests while iterating, then the complete affected application suite after behavior changes.
- For editor API or bridge behavior, run the relevant headless Godot checks. Use the opt-in subprocess integration test when the cross-process boundary changes.
- Keep stdout protocol-only and diagnostics on stderr.

## 4. Update the knowledge cache

Update documentation in the same change as the implementation:

- Component ownership, dependencies, invariants, or verification changed: update the owning `architecture/*.md` file.
- A custom type, wire record, collaborator protocol, or reusable function library changed: update its file under `types/<component>/`.
- A new architecture or type file was added: update only the immediate parent `index.md`.
- User-visible installation or operation changed: update `README.md`.
- Released or planned scope changed: update `HISTORY.md` or `ROADMAP.md` as required.

Every directory under `docs/` must contain an `index.md`. An index describes only its immediate files and subdirectories; it does not duplicate deeper inventories. Keep links relative and never recreate the removed `CODE.md` monolith.

## 5. Release consistency

When a roadmap phase changes behavior, update the application version according to the repository rules and keep Python metadata, plugin metadata, runtime-reported versions, tests, README examples, and history synchronized. Documentation is explanatory and must not be used as executable release input or a test fixture.
