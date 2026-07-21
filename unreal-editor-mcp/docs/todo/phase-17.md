# Phase 17 — GameInstance family

**Outcome:** The established workflow supports UObject-based GameInstance Blueprints without weakening Actor-family restrictions or assuming component support.

### Implementation

- Add the `UGameInstance` family through the Phase 16 family-policy and capability-matrix contracts.
- Evaluate live GameInstance capabilities for defaults, components, event graphs, local variables, overrides, graph types, and supported actions. Reject component operations and every other unsupported operation before mutation.
- Reuse inspection, class defaults, members, action catalog, graph editing, compile, save, diagnostics, operation reconciliation, and security contracts without introducing a separate mutation path.
- Add GameInstance-specific default properties, callbacks, override functions, and graph-action coverage. Keep every output family-aware.

### Verification

- Create, inspect, edit defaults and logic, compile, save, restart, and read back representative GameInstance Blueprints.
- Test callbacks, inherited functions, class defaults, explicit component-operation rejection, local-variable and graph capabilities, parent changes outside scope, and manual project-settings assignment.
- Run the complete shared and GameInstance-specific suites natively on macOS and Windows and require identical normal model-facing contracts.

### Documentation and completion gate

- Document GameInstance capabilities, component differences, default-property use cases, callbacks, and focused examples. Do not add project-settings mutation.
- Complete the phase only when GameInstance passes the shared contract and its family-specific restrictions on both native platforms.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
