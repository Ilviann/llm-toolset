---
feature_id: blueprint-library-inspect
status: completed
depends_on:
  - phase-6
  - phase-7
released_in: "0.37.0"
---

# `blueprint-library-inspect` — Blueprint function and macro library inspection

**Outcome:** Agents can discover and inspect Blueprint Function Library functions and Blueprint Macro Library macros, including their signatures and bounded graph structure, without granting library mutation authority.

**Status:** Completed in 0.37.0 with Windows verification. macOS verification remains in the native platform backlog.

**Depends on:**

- [`phase-6`](phase-6.md) for function signatures, parameters, locals, identities, and references.
- [`phase-7`](phase-7.md) for macro signatures, parameters, identities, and references.

### Implementation

- Publish `function_library` and `macro_library` as base inspection-only families after the seven authoring families.
- Classify unloaded discovery candidates from the bounded Asset Registry `BlueprintType` tag and classify loaded assets from `UBlueprint::BlueprintType`, so an Actor-scoped macro library cannot be mistaken for an Actor Blueprint.
- Reuse the ordinary function, macro, parameter, local-variable, graph, node, pin, connection, snapshot, cursor, response-bound, and non-mutation collectors.
- Publish declaration records as non-editable and reject compile, save, member editing, action cataloging, graph editing, and function replacement before mutation.

### Verification

- `UnrealMCP.BlueprintLibraries.InspectionOnlyFamilies` creates representative function and Actor-scoped macro libraries, verifies discovery and exact family classification, inspects declarations and graph structure, checks non-editable records and stable mutation rejection, and proves inspection preserves Blueprint state.
- Run Python contract and schema tests, documentation lint, adaptive and forced-unity Editor builds, the focused native test, the full `UnrealMCP` Automation suite, and the production headless workflow.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
