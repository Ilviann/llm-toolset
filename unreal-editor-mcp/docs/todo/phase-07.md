# Phase 7 — Macros and custom events

**Outcome:** Agents can inspect and define macro shells and custom-event shells without confusing them with inherited, override, interface, or function-owned logic.

### Implementation

- Extend `blueprint_inspect` with bounded macro and custom-event signatures, parameters, metadata, ownership, graph relationships, required nodes, and reference summaries.
- Add `blueprint_member_edit` operations for one add, rename, supported update, or safe removal of a macro or custom-event shell.
- Create macro graphs through public Blueprint utilities, preserve required tunnel nodes, and validate complete signatures before mutation.
- Create custom events only in compatible event graphs. Keep custom events, override events, interface functions, inherited members, functions, and macros distinct.
- Reuse the canonical K2 vocabulary for parameters and defaults. Use `reject_if_referenced` for signature changes and removal.
- Return concise member, graph, parameter, reference, reconstruction, and snapshot records.

### Verification

- Test macro signatures and tunnels, custom-event parameter forms and graph restrictions, inherited and cross-kind collisions, referenced members, safe removal, and unsupported capabilities.
- Test undo/redo, compilation, saving, restart, read-back, and preservation after every rejection and injected failure.
- Prove that inspection exposes enough information to plan every accepted macro or custom-event mutation.

### Documentation and completion gate

- Document function, macro, custom-event, override-event, and interface distinctions with focused inspect-before-edit examples.
- Complete the phase only when empty macros and custom-event shells survive compile, save, restart, and exact bounded inspection.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
