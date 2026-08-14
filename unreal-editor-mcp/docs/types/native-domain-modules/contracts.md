# Native domain module contracts

## Built-in module interface

`IUnrealMCPBuiltInDomainModule` exposes a stable domain name,
`RegisterAssetFamilies`, and `RegisterCommands`. Implementations are ordinary
repository-owned Unreal editor modules with `LoadingPhase: None`; the host owns
their exact loading order and lifetime.

## Registrar

`FUnrealMCPDomainRegistrar` is the only command-composition boundary. It accepts
typed command descriptors, Boolean feature resolvers, finite numeric limits,
and one Blueprint-family capability provider. It exposes no bridge, HTTP,
credential, plugin-lifecycle, or model-schema mutation API.

## Context

`FUnrealMCPDomainContext` supplies the project and bridge identities, the frozen
asset-family registry, the narrow Blueprint companion-extension provider, and
the retained-operation concurrency refusal callback. Domains retain only the
state needed by their lazy services.

## Ordering and failure

Every built-in command has one non-negative unique `Order`. Catalog freeze
sorts descriptors by that order and rebuilds lookup indexes, so module load
order cannot change the released command sequence. Null or duplicate domains,
duplicate commands or order values, invalid capabilities or limits, a missing
Blueprint-family provider, and registration after freeze fail bridge startup.

## Ownership

Asset Core registers `asset_inspect` and the neutral family. Blueprint
registers the core Blueprint family, Blueprint commands, and gameplay-framework
editing. UMG registers Widget authoring. Content registers asset references and
deletion, Levels, and Game Data. Host-only commands remain capabilities,
editor state/shutdown, and operation status/cancellation.
