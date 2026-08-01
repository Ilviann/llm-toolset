# Shared command infrastructure types and libraries

- [Type: error envelope](contracts.md#type-error-envelope) — stable Godot success/failure result type.
- [Library: error envelopes](contracts.md#library-error-envelopes) — envelope construction, bounding, and legacy-classification helpers.
- [Library: command limits](contracts.md#library-command-limits) — centralized editor-side resource limits.
- [Library: project identity](contracts.md#library-project-identity) — normalized project hash library.
- [Library: atomic JSON records](contracts.md#library-atomic-json-records) — bounded crash-safe record library.
- [Type: cursor record](contracts.md#type-cursor-record) — opaque pagination continuation records.
- [Library: cursor store](contracts.md#library-cursor-store) — cursor issue, preflight, resume, expiry, and eviction helpers.
- [Type: operation record](contracts.md#type-operation-record) — accepted/completed asynchronous operation records.
- [Library: operation registry](contracts.md#library-operation-registry) — operation allocation, completion, recovery, and concise views.
- [Type: event record](contracts.md#type-event-record) — monotonic editor event records.
- [Library: event store](contracts.md#library-event-store) — event allocation and bounded history helpers.
- [Type: property value contract](contracts.md#type-property-value-contract) — bounded model-facing Godot Variant value type.
- [Library: property value codec](contracts.md#library-property-value-codec) — Variant conversion and encoding helpers.
- [Type: input event contract](contracts.md#type-input-event-contract) — normalized Input Map event type.
- [Library: input event codec](contracts.md#library-input-event-codec) — event decoding and normalization helpers.
- [Library: project-path guard](contracts.md#library-project-path-guard) — confined project-path validation helpers.
- [Library: scene-node access](contracts.md#library-scene-node-access) — confined edited-scene path/name helpers.
