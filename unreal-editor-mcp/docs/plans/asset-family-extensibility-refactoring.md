# Asset-family extensibility refactoring

This design plan preserves the accepted direction behind the related planned
features. It is not an executable or released contract. Current behavior remains
defined by source, schemas, metadata, tests, and completed feature documents;
each linked planned feature owns its releasable requirements.

## Primary goal

Make support for a new Unreal asset family fast and predictable to implement:

- inspect a new asset type through bounded semantic projections;
- create a new asset type through a shared mutation and persistence lifecycle;
- edit an existing asset through stale-safe typed operations and read-back;
- add optional Engine-plugin-dependent families without adding those dependencies
  to the base plugin.

Shorter native compilation and linking are useful secondary outcomes of clearer
module ownership. They are not the criterion for choosing abstractions or
splitting modules.

## Target boundary

Transport JSON belongs only at explicit protocol codecs. Native domain services,
asset-family adapters, authoring infrastructure, and companion handlers exchange
bounded Unreal-native request and result records instead of constructing
`FJsonObject` responses in place.

```text
MCP schema and JSON
        |
        v
explicit native codec
        |
        v
typed request -> domain service/kernel -> asset-family adapter
        |                                      |
        +----------- typed result <------------+
                        |
                        v
                explicit native codec
                        |
                        v
                 JSON and MCP result
```

The host continues to own authentication, access policy, exact target resolution,
dispatch, mutation replay, transactions, persistence authority, response bounds,
wire schemas, encoding, and capability collision policy. An adapter owns only the
semantics specific to its asset family.

## Extension model

Every asset family has one stable descriptor with exact class policy, dependency
requirements, limits, and independent inspection, creation, and editing
capabilities. Inspection support does not imply authoring support.

Built-in families register through a base-owned frozen registry. Optional families
register through the exact companion API. Python publishes a matching compact
catalog that maps model-facing schemas to native family and operation identities;
it does not reproduce Unreal domain logic.

A new family should normally require only:

1. one descriptor and the adapters for the capabilities it supports;
2. focused native semantic records or operations owned by its domain module;
3. one Python catalog/schema contribution;
4. conformance fixtures plus genuinely family-specific tests;
5. user and architecture documentation for the new behavior.

Adding a family should not require editing the bridge dispatcher, duplicating
transaction/save/replay code, constructing transport JSON in the adapter, or
adding an optional Engine-plugin dependency to the base module.

## Planned delivery sequence

1. [`native-wire-contracts`](../features/completed/native-wire-contracts.md) moves
   JSON conversion to explicit transport codecs.
2. [`native-command-catalog`](../features/completed/native-command-catalog.md)
   replaces centralized command branching with deterministic typed registration.
3. [`asset-family-foundation`](../features/completed/asset-family-foundation.md)
   defines descriptors, registries, and inspection/creation/edit adapter seams.
4. [`asset-inspection-adapters`](../features/completed/asset-inspection-adapters.md)
   migrates built-in inspection behind the family seam.
5. [`asset-authoring-kernel`](../features/completed/asset-authoring-kernel.md)
   centralizes target-independent creation and stale-safe editing lifecycles.
6. [`python-asset-family-catalog`](../features/completed/python-asset-family-catalog.md)
   makes Python publication data-driven and consistent with native capabilities.
7. [`asset-family-conformance`](../features/completed/asset-family-conformance.md)
   supplies reusable inspection, authoring, persistence, and unavailable-state
   verification.
8. [`native-domain-modules`](../features/completed/native-domain-modules.md)
   moves established domains behind narrow module boundaries after the seams are
   proven.
9. [`companion-api-v2`](../features/completed/companion-api-v2.md) migrates every
   companion and fixture together to the complete typed family API.
10. [`companion-asset-adapters`](../features/completed/companion-asset-adapters.md)
    proves inspection and authoring for optional families without dependency
    leakage into the base.

Items 4 through 6 may proceed in parallel after the family foundation, but the
conformance feature depends on all three. The companion API migration remains a
single exact-version transition: no v1/v2 compatibility range or mixed companion
set is planned.

## Design invariants

- Keep model-facing tools few and stable; asset-family growth occurs behind shared
  facades rather than by adding one tool per Unreal class.
- Use explicit bounded codecs. Do not serialize arbitrary `UObject` or `USTRUCT`
  state or use unrestricted reflection as a general authoring API.
- Keep semantic family logic separate from transport, policy, persistence, and
  editor lifecycle ownership.
- Preserve deterministic identities, selectors, ordering, fingerprints, limits,
  errors, and postcondition read-back across native and Python boundaries.
- Fail closed on ambiguous classification, missing dependencies, stale state,
  registration collisions, incompatible companion APIs, and capability mismatch.
- Split native modules only around proven domain ownership and dependency
  boundaries; do not create small libraries solely to reduce line counts.
- Apply the reusable conformance suite to every family while retaining focused
  tests for family-specific Unreal behavior.

## Using this plan

Before implementing a new inspection or authoring feature, identify the family,
its built-in or companion ownership, supported capabilities, required Engine
modules, semantic records, limits, persistence behavior, and conformance fixture.
Then follow the relevant planned feature dependencies in the
[`ROADMAP.md`](../../ROADMAP.md). If implementation evidence changes this design,
update this plan and the affected feature documents together before expanding
scope.

[Back to plans](index.md) · [Feature catalog](../features/index.md) · [Roadmap](../../ROADMAP.md)
