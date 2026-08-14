# Native domain modules

## Ownership

The base plugin is one external plugin with five editor modules:

- `UnrealMCP` is the host and composition root. It owns transport,
  authentication, lifecycle, companion API v2, extension discovery, and final
  command-catalog freeze.
- `UnrealMCPAssetCore` owns neutral wire records, JSON codecs, the asset-family
  registry, the authoring kernel, the operation ledger, and neutral asset
  inspection.
- `UnrealMCPBlueprint` owns Blueprint inspection, policy, graph authoring,
  mutation, gameplay-framework assignment, and shared reflected value codecs.
- `UnrealMCPUMG` owns Widget tree, layout, style, and binding authoring.
- `UnrealMCPContent` owns asset references/deletion, Levels, user-defined
  structs, and Data Tables.

Each module keeps its native Automation Tests under its own `Private/Tests/`
directory. Public headers exist only for typed cross-domain contracts.

## Composition and dependency direction

All four domain descriptors use `LoadingPhase: None`. During host startup,
`UnrealMCPModule.cpp` explicitly loads Asset Core, Blueprint, UMG, then Content.
The host asks each module to register asset families, freezes the family
registry, discovers companions, and then composes commands through
`FUnrealMCPDomainRegistrar`.

Asset Core is the inward shared boundary. Blueprint depends on Asset Core; UMG
and Content depend on Asset Core and Blueprint; none depends on the host.
`UnrealMCP` has a static Asset Core dependency, a private Blueprint dependency
for its companion compatibility boundary, and dynamic UMG/Content dependencies.

## Invariants

- Built-in load order and command order are explicit and independent. Command
  descriptors carry unique numeric order values before catalog freeze.
- Domain names, commands, feature names, limits, and the Blueprint-family
  provider are validated globally and fail startup closed on conflicts.
- Companions remain independently loaded after built-in family freeze and
  cannot register modules, native commands, or model-facing schemas.
- Adding a leaf-domain service or test does not require host or unrelated-domain
  source changes. A new domain module is added only with its first real adapter.
- The external plugin name, companion API v2, Python tool catalog, and installed
  base-plugin directory remain unchanged.

## Verification

Release contracts verify module descriptors, load order, ownership, dependency
direction, and package binaries. UE builds run adaptive, forced-unity, and
non-unity modes; native Automation, complete headless integration, and base,
GAS, and CommonUI packaging verify behavior and distribution.
