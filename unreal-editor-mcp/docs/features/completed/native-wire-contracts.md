---
feature_id: native-wire-contracts
status: completed
depends_on: []
released_in: "0.37.0"
---

# `native-wire-contracts` — Typed native request and result records

**Outcome:** Native domain services consume JSON-neutral requests and return JSON-neutral semantic results while explicit bounded codecs own JSON conversion at the transport boundary.

**Implementation status:** Completed in 0.37.0. Windows passed the Python suite, focused and full native Automation, adaptive/forced-unity/non-unity UE 5.8 builds, full headless lifecycle integration, and isolated Win64 base-plugin packaging. macOS remains preferred follow-up work and Linux is out of scope.

### Implementation

- `FUnrealMCPValue` and `FUnrealMCPRecord` own bounded JSON-neutral scalar, array, record, request, result, error, capability, identity, selector, paging, diagnostic, mutation, and persistence contracts.
- `UnrealMCPJsonCodec` is the explicit recursive JSON boundary. It rejects excessive depth, fields, items, total values, strings, and non-finite numbers; it never reflects arbitrary `UObject` or `USTRUCT` state.
- Base-plugin domain services, operation retention, capability construction, diagnostics, mutation results, and persistence read-back use neutral records. `UnrealMCPProtocol` alone parses and emits HTTP JSON.
- Companion API v1 remains unchanged and JSON-coupled. `UnrealMCPExtensionRegistry` explicitly adapts between v1 JSON and neutral base records, including bounded companion results and error details; `companion_api_version` remains `1`.
- The model-facing command schemas, field ordering, omissions, stable errors, bounds, and pretty JSON envelopes remain unchanged.

### Verification and completion gate

- `UnrealMCP.WireContracts` covers recursive round trips, common typed records, bounded invalid strings and numbers, typed command decoding, and complete success/error envelope fixtures.
- Existing native tests now construct and verify neutral service records, retaining full behavioral coverage across every base domain.
- Windows validation covers the Python suite, adaptive/forced-unity/non-unity UE 5.8 builds, full native Automation, full headless integration, and isolated base packaging.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
