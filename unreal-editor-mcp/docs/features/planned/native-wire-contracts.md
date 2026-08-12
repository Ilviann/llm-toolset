---
feature_id: native-wire-contracts
status: planned
depends_on: []
released_in: null
---

# `native-wire-contracts` — Typed native request and result records

**Outcome:** Native domain services consume typed requests and return typed semantic results while explicit codecs own JSON conversion at the transport boundary.

### Implementation

- Add JSON-neutral request, result, error, capability, identity, selector, paging, diagnostic, mutation, and persistence records.
- Keep `FJsonObject` inside protocol and domain-codec components. Migrate base-plugin service interfaces and inline response construction without changing model-facing schemas, field ordering, omissions, errors, or bounds.
- Use explicit bounded encoders and decoders. Do not serialize arbitrary `UObject` or `USTRUCT` state, use unrestricted `FJsonObjectConverter`, or expose live Unreal objects as wire contracts.
- Retain companion API v1 as the only temporary JSON-coupled compatibility boundary until `companion-api-v2` migrates the complete companion set.

### Verification and completion gate

- Add round-trip and invalid-input tests for every common record and compare complete native response fixtures before and after migration.
- Run the Python suite, adaptive/forced-unity/non-unity Windows builds, native Automation, full headless integration, and base packaging.
- Complete only when base domain services no longer construct transport JSON directly and all released behavior remains unchanged.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
