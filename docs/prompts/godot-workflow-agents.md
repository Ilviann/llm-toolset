# Godot Implementation and Knowledge Rules

### Before implementation

- Read the task requirements and retrieve only the relevant knowledge blocks.
- Identify affected scenes, scripts, Resources, Autoloads, signals, input actions, and project settings.
- Inspect the existing implementation before proposing new architecture.
- Follow established project patterns unless there is a documented reason to change them.

### Implementation

- Build features as small, cohesive scenes with clear ownership.
- Prefer composition of scenes and nodes over deep inheritance.
- Use `Resource` types for reusable data and configuration; keep runtime state in its owning node or system.
- Use direct method calls inside a clear ownership hierarchy and signals for events crossing ownership boundaries.
- Use Autoloads only for services or state that genuinely require project-wide access and cross-scene lifetime.
- Avoid hidden dependencies, arbitrary node-tree searches, fragile node paths, and unnecessary global state.
- Keep gameplay logic separate from presentation where practical.
- Use typed code, explicit interfaces, and descriptive names consistent with the project.
- Respect Godot lifecycle, input, physics, threading, serialization, and resource-loading rules.
- Do not add abstractions for hypothetical future needs.
- Profile before making performance-driven architectural changes.
- Preserve compatibility of saved data, Resources, scene paths, exported properties, and signal contracts.

### Knowledge updates

- Create or update knowledge only for durable, non-obvious information.
- Keep each knowledge block focused on one feature, component, flow, or contract.
- Do not restate declarations or behavior that is immediately obvious from source code.
- Record:
  - purpose and ownership;
  - important scenes, scripts, Resources, and Autoloads;
  - public entry points and signals;
  - runtime flow;
  - invariants and constraints;
  - dependencies and integration points;
  - known pitfalls;
  - validation steps.
- Link knowledge to authoritative source paths and stable symbol names.
- Keep each block independently understandable and normally below 300 words.
- Split large blocks by responsibility rather than expanding them indefinitely.
- Remove or correct stale knowledge when implementation changes.

### Completion

- Run relevant tests and project validation.
- Verify affected scenes in the editor or appropriate runtime configuration.
- Check errors, warnings, broken resources, signal connections, and scene references.
- Confirm that documentation indexes reference every changed knowledge block.
- Finish only when code, assets, configuration, tests, and knowledge describe the same behavior.
