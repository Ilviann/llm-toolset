# Native command catalog contracts

## Descriptor

`FUnrealMCPCommandDescriptor` binds one exact identity to `ReadOnly`, `Writable`, or `Internal` access; `RequestThread` or `GameThread` dispatch; `None` or `Retained` operation policy; one typed neutral-record handler; optional live Boolean native features; and finite numeric native limits. `bAllowsExtensionRequests` is fixed and true only for the existing `asset_inspect` companion seam.

## Registration and freeze

`FUnrealMCPCommandCatalogBuilder::Register` accepts only `FixedNative` descriptors and rejects any model-schema contribution. Identities, feature names, and limit names are globally unique. Every descriptor requires a non-empty identity and handler. `Freeze` revalidates composed feature and limit names, makes mutable lookup unavailable, and rejects every later registration.

## Composition

Descriptors retain insertion order. That order supplies `capabilities.commands`; descriptor feature and limit contributions supply `capabilities.features` and `capabilities.limits`. The host appends exact companion records and Blueprint-family records but companions cannot register native commands or schemas. Python independently retains the shipped model-facing schemas and verifies exact access parity.

## Errors

Catalog construction fails bridge startup with a bounded local error for duplicate commands, conflicting capabilities or limits, invalid descriptors, runtime commands or schemas, and late or repeated freeze attempts. Runtime lookup of an absent identity retains the existing `invalid_argument` and `Unknown or unavailable command` contract.
