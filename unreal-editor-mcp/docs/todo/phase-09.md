# Phase 9 — C++ architecture and test decomposition

**Outcome:** The native plugin and Automation Tests are divided into small cohesive implementation units that preserve every Phase 1–8 runtime, security, wire, snapshot, transaction, persistence, and verification contract while providing stable extension points for action-family and graph-editing work.

### Implementation

- Keep `FUnrealMCPBlueprintMutator`, `FUnrealMCPBlueprintInspector`, and `FUnrealMCPBlueprintActionCatalog` as the bridge-facing facades. Move internal behavior into private typed collaborators and family-focused translation units without expanding the public module API or model-facing command surface.
- Split Blueprint mutation into shared target/scope/snapshot/result infrastructure plus lifecycle, component/default, member-variable, function, local-variable, macro, and custom-event implementations. Preserve injected compile/save collaborators, exact prevalidation, one-transaction semantics, explicit failure restoration, and authoritative inspection-based postconditions.
- Introduce one bounded typed Blueprint-reference scanner for variables, functions, locals, macros, and custom events. Use it from both inspection and mutation; encode its result to JSON only at the wire boundary rather than using JSON objects for internal mutation decisions.
- Replace the monolithic inspection builder with a typed normalized query, a bounded record/fingerprint sink, and focused discovery, component/default, member/callable, and graph collectors. Preserve one structural snapshot across every requested section and retain cursor state only in the inspector facade.
- Separate action-catalog query decoding, live Blueprint/graph/pin resolution, bounded scanning, family classification/record encoding, and retained-cache identity management before adding more action families in Phase 10.
- Move shared native test fixture construction, argument builders, inspection helpers, mutation execution, snapshot tracking, and cleanup into test-support files. Split Automation Tests by owning component and divide long function/local/RepNotify, macro/custom-event, and action-catalog scenarios into independently named cases under their existing `UnrealMCP.PhaseN` prefixes.
- Keep cohesive codecs, bridge lifecycle, protocol, operation-ledger, token, discovery, and compatibility components intact unless extraction removes demonstrated duplication. Do not create generic utility layers that weaken component ownership or validation policy.
- Use named internal namespaces or shared private helpers that remain valid under Unreal unity builds; do not duplicate anonymous-namespace symbols across files that may be combined into one unity translation unit.
- Update the affected architecture pages, type/library references, automated-verification documentation, and their immediate indexes with the final source ownership and dependency direction.

### Verification

- Capture a clean baseline before refactoring, then run the complete Python suite, compile the disposable Editor target, run all `UnrealMCP` Automation Tests, and run the complete cross-process integration workflow after the split.
- Compile the affected module with its normal Unreal Build Tool unity configuration and with unity disabled. Prove that every new private header has explicit dependencies and that no translation unit relies on unity-build include leakage or colliding anonymous-namespace helpers.
- Preserve exact capabilities, command schemas, errors, limits, snapshots, reference summaries, mutation results, operation replay, compile/save behavior, package state, selection state, Undo/Redo, and restart read-back for the Phase 1–8 fixtures.
- Prove the decomposed test suite retains all existing assertions and automation filters, creates no committed generated fixture, and reports narrower failures without introducing order dependence between cases.
- Review the affected implementation units after extraction. Target at most 600 lines per new or substantially rewritten translation unit and at most 200 lines per orchestration function; document any cohesive exception rather than splitting code mechanically.

### Documentation and completion gate

- Document the facade/collaborator boundaries, shared reference-scanner contract, inspection collector/snapshot flow, action-family extension point, test-support ownership, and unity/non-unity build expectations.
- Complete the phase only when the oversized mutator, inspector, and monolithic Automation Test translation units have been removed, action-catalog growth has a focused family extension boundary, duplicated reference scanning has been eliminated, and the full native and cross-process verification gates pass without a model-facing behavior change.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
