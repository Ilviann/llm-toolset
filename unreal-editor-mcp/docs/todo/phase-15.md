# Phase 15 — GameMode and GameState families

**Outcome:** The established Actor-derived workflow formally supports GameMode and GameState Blueprint families through an explicit published family policy.

### Implementation

- Replace Actor-only eligibility checks in discovery, inspection, creation, compile, save, and mutation resolution with an explicit published family policy.
- Formally classify and qualify `AGameModeBase`/`AGameMode` and `AGameStateBase`/`AGameState` within the existing Actor-derived path rather than treating them as a new inheritance category.
- Evaluate live Blueprint capabilities for defaults, components, event graphs, local variables, overrides, and graph types. Publish a family/operation capability matrix and reject unsupported operations before mutation.
- Reuse inspection, class defaults, components, members, action catalog, graph editing, compile, save, diagnostics, operation reconciliation, and security contracts.
- Add family-specific default properties, callbacks, override functions, and graph-action coverage. Keep every output family-aware.

### Verification

- Create, inspect, edit defaults and logic, compile, save, restart, and read back representative GameModeBase, GameMode, GameStateBase, and GameState Blueprints.
- Test framework callbacks, inherited functions, class defaults, component operations, local-variable and graph capabilities, parent changes outside scope, and manual project-settings assignment of saved classes.
- Run the complete shared and family-specific suites natively on macOS and Windows and require identical normal model-facing contracts.

### Documentation and completion gate

- Document the family capability matrix, default-property use cases, callbacks, component behavior, and focused examples. Do not add project-settings mutation.
- Complete the phase only when all four GameMode and GameState families pass the shared contract and family-specific restrictions on both native platforms.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
