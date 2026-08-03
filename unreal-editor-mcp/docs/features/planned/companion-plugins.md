---
feature_id: companion-plugins
status: planned
depends_on:
  - readonly-mode
released_in: null
---

# `companion-plugins` — Companion plugin extension foundation

**Outcome:** API-compatible independently versioned editor-only companion plugins can be discovered and can safely register bounded support for additional asset types, component types, and functionality on existing assets through the base Unreal MCP bridge.

**Depends on:**

- [`readonly-mode`](../completed/readonly-mode.md)

### Discovery, identity, and lifecycle

- Add one narrow public native extension API and a base-owned registry. An enabled editor companion declares a required `UnrealMCP` plugin dependency without a semantic-version pin, a stable lowercase ASCII extension ID using only alphanumerics, `-`, and `_`, one supported extension-schema revision, its owning module, its independent companion semantic version, and one explicit positive integer `companion_api_version` whose initial value is `1`; its editor module registers through that API during startup.
- Discover enabled companion descriptors through Unreal's plugin manager, but never load a disabled module merely because its descriptor is installed. Admit a registration only when the owning plugin and module identities match discovery, the base and companion descriptor `companion_api_version` values match exactly, the compiled base and companion constants match each other and their descriptors, the schema revision is supported, and every declared Engine-plugin dependency is effectively enabled and loaded for the configured project. Semantic-version differences between the base and companion are valid.
- Freeze the registry before bridge readiness and capability publication. Reject duplicate extension IDs, duplicate type or operation ownership, conflicting contributors, undeclared modules, late registration, and registration after shutdown begins. Require an editor restart after plugin enablement changes; do not support hot enablement, hot reload, or live replacement in this foundation.
- Sort accepted companions and every contribution deterministically by stable identity. Bound discovered descriptors, companions, contributions per companion, capability records, schema IDs, and registration diagnostics. One absent, disabled, rejected, or failed companion must not prevent the base plugin or another valid companion from becoming ready.
- Keep listener, credential, route, authentication, request queue, Game-thread dispatch, operation ledger, errors, limits, discovery records, shutdown, and model-facing capability composition owned by the base plugin. A companion cannot add another listener, credential, HTTP route, process, Python package, lifecycle controller, or durable state root.

### Typed extension contracts

- Define separate explicit registration records for new asset families, new component families, and contributions to existing asset families. Every record names one base-owned tool family, exact read and mutation operations, access classification, target class policy, handler identity, required live capability, stable limits, and snapshot or preservation participation.
- New asset-family registration may contribute bounded discovery, classification, inspection sections, stable identities, snapshots, references, creation policy, exact typed mutations, action-catalog entries, compilation policy, saving, and postcondition read-back only through applicable existing asset or Blueprint tool families.
- New component-family registration may contribute exact component classification and bounded typed inspection or mutation for Blueprint templates or level actors. It must declare which owners, inheritance forms, construction sources, persistence boundaries, and structural operations are supported; unsupported native, inherited, construction-script, runtime, or generated components fail closed.
- Existing-asset registration may add bounded inspection sections, preservation fingerprints, capability fields, exact typed operation discriminators, or action contributors to an explicitly named released asset family. It cannot replace base handlers, relax base policy, reinterpret an existing operation, mutate outside its declared section, or omit companion-owned state from stale snapshots and rollback verification.
- Route every accepted handler through base path and mount confinement, exact argument validation, access-mode filtering, capability checks, Game-thread dispatch, limits, transactions where applicable, operation-ledger admission, compile and save policy, postcondition inspection, rollback, replay, and stable error mapping. Companion validation may narrow these contracts but cannot bypass or broaden them.

### Python schemas and capability composition

- Keep model-facing schemas in the exact-version Python package. Ship a bounded allowlisted catalog of known extension IDs, schema revisions, operation discriminators, read or write classification, and response contracts; intersect that catalog with the native registry reported through `capabilities`. Never accept a runtime-provided JSON Schema, tool definition, Python module, property path, reflected function, or arbitrary command from a companion.
- Extend existing compact inspect and edit tools instead of publishing one tool per companion or native handler. Reject an enabled native extension that the matching Python catalog does not know, and reject a Python-known extension whose native registration is absent, mismatched, unavailable, or stale.
- Publish a bounded ordered `companions` capability list containing plugin and extension identities, independent semantic versions, `companion_api_version`, schema revisions, effective readiness, required Engine-plugin states, registered target families and operations, read support, mutation support, limits, and stable unavailable reasons. Do not publish filesystem paths, module load addresses, credentials, unrestricted class inventories, or unbounded diagnostics.
- Compose asset, Blueprint-family, component, action, feature, and command capabilities from accepted registrations without changing base ownership. Readonly mode may publish admitted inspection contributions; mutation operations remain absent and undispatchable unless both the companion reports mutation readiness and Python started with explicit writable access.

### Verification

- Add a disposable editor-only test companion that registers one new test asset family, one new test component family, and one bounded inspection and mutation contribution to an existing test asset family. Use it to prove discovery, exact schema intersection, deterministic capability composition, stale-safe read-back, preservation, transaction and ledger behavior, and clean unregister-on-shutdown behavior without making the fixture a released asset contract.
- Test absent, installed-but-disabled, enabled, missing-module, wrong owner, missing descriptor API version, descriptor API mismatch, compiled API mismatch, descriptor-versus-compiled disagreement, unsupported schema revision, independently different semantic versions, inconsistent companion-owned version sources, missing or disabled Engine dependency, duplicate ID, target or operation collision, excessive registration, late registration, startup failure, stale capability, shutdown ordering, and plugin-state changes requiring restart.
- Test every extension category with normal, invalid, limit, security, stale-state, timeout, replay, lost-response, rollback, compile or save failure, and unchanged-content cases. Prove one rejected or failing companion cannot expose partial operations, corrupt registry state, weaken base validation, or break the complete base suite.
- Assert exact `tools/list`, schema validation, and native dispatch behavior for readonly and writable modes with no companion, an inspection-only companion, a mutation-capable companion, mixed valid companions, and rejected companions. Unknown extension IDs and forged operations must reject before bridge dispatch or object loading.
- Build and package the base and fixture companion with normal, forced-unity, and non-unity editor builds. Run public-header probes, native Automation, complete Python and base suites, and production-bridge cross-process acceptance on Windows. Repeat on macOS when available as non-blocking follow-up; no Linux verification or source-portability evidence is required.

### Documentation and completion gate

- Document companion installation, effective project enablement, independent semantic versioning, strict `companion_api_version` equality, descriptor and compiled checks, discovery and restart behavior, extension and schema identities, capability detection, read versus mutation availability, access-mode interaction, limits, stable unavailable reasons, packaging, and troubleshooting.
- Document the supported extension categories and base-owned invariants for authors of future in-repository companions. State that companions are trusted native project code but all model input remains untrusted, and that Unreal MCP never downloads, installs, enables, or hot-loads companions or their Engine dependencies at runtime.
- Complete the feature only when the fixture proves all three extension categories through the existing authenticated bridge, rejected companions expose no partial functionality, readonly and writable filtering remain authoritative, descriptor or compiled API-version and schema mismatches fail closed while independent semantic versions remain valid, the base plugin remains fully usable without companions, and the complete suites pass on both native platforms.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
