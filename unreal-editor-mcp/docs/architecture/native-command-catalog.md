# Native command catalog

## Ownership

`UnrealMCPCommandCatalog` owns the frozen native command descriptors and typed composition of built-in domain contributions. Each descriptor binds one exact command identity and explicit order to its access class, request-thread or Game-thread dispatch policy, retained-operation policy, handler, native feature fields, and native limits. Registration accepts only compiled base-plugin contributions and freezes before the bridge starts.

## Dependency direction

`UnrealMCPBridge` depends on the frozen catalog for command lookup, routing, feature publication, limits, and Blueprint-family capability composition. The catalog depends on typed `IUnrealMCPBuiltInDomainModule` contributions and the companion extension registry; domain modules and services do not depend on the catalog implementation or bridge. Python remains authoritative for model-facing tool schemas and startup access filtering.

The bridge retains authentication, request/body and response bounds, queue admission, Game-thread scheduling, operation-ledger admission and result retention, listener lifecycle, and the final capability envelope. Request-thread operation status and cancellation use catalog descriptors while remaining bridge-admitted host operations.

## Invariants

- Command order is explicit, deterministic across module load order, and matches the released Python catalog plus internal `editor_shutdown`.
- Every command has exactly one explicit access, dispatch, retained-operation, and handler policy.
- Duplicate identities, duplicate feature or limit names, late registration, runtime commands, and native schema contributions fail closed.
- The frozen catalog cannot be mutated. Optional companions may contribute bounded results and live capability values only through the existing exact companion registry; they cannot add commands or schemas.
- Adding another fixed base-domain handler changes its owning domain, not `UnrealMCPBridge` or unrelated domains.

## Verification

`UnrealMCP.CommandCatalog.FixedCompositionAndRejection` covers deterministic composition and all registration refusals. Python release contracts prove exact command order, access, dispatch, retained-operation, feature, limit, and bridge-domain-neutrality parity. Full native Automation, lifecycle, headless, build, and packaging gates cover runtime behavior.
