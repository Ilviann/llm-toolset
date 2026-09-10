---
feature_id: blueprint-interface-inspect
status: active
depends_on:
  - blueprint-library-inspect
released_in: null
---

# `blueprint-interface-inspect` — Blueprint Interface asset inspection

**Outcome:** Discover Blueprint Interface assets and inspect their declarations, typed signatures, metadata, and selected graphs without granting authoring authority.

**Depends on:**

- [`blueprint-library-inspect`](../completed/blueprint-library-inspect.md) for inspection-only family admission and shared callable collectors.

## Checklist

- [x] Classify unloaded and loaded interface assets by Blueprint type and publish the inspection-only family and feature capability.
- [x] Reuse bounded function/parameter/graph records, identities, snapshots, and cursors; preserve interface ownership and valid void declarations.
- [x] Reject authoring and expose no defaults, components, locals, or replacement authority.
- [x] Add native signature, graph selection, paging, stale cursor, and non-mutation coverage plus a production-client restart scenario.
- [x] Pass Python tests, documentation lint, Windows adaptive/forced-unity builds, native Automation, interface restart acceptance, and binary packaging.
- [ ] Pass the full headless integration release gate.
- [ ] Complete release metadata and record preferred macOS verification in the platform backlog.

[Back to roadmap](../../../ROADMAP.md)

## Validation status

Interface-specific restart acceptance verifies unloaded discovery, input/output and void signatures, graph paging, mutation rejection, stable snapshots, and unchanged asset bytes. Full headless acceptance remains pending: framework action cataloging required a fresh bounded retry after an explicit timeout, and later runs encountered a native Slate layout crash or a transient bridge-not-ready response in existing graph authoring. These failures are outside interface inspection; the feature is not marked completed or released.
