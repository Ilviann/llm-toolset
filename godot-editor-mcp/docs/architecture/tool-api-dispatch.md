# Tool API policy and dispatch

## Purpose

Define the model-facing tool surface once, derive bounded schemas and modes, validate calls, map them to focused handlers, and coordinate optional waits and capture consumption.

## Owned source

- `godot_editor_mcp/tool_catalog.py` — `ToolSpec` registry, schemas, mode subsets, bridge routes, path/wait policy, and expected live contracts.
- `godot_editor_mcp/schema_validation.py` — dependency-free validator for the published schema vocabulary.
- `godot_editor_mcp/tool_dispatch.py` — local/bridge routing, preflight checks, waits, capability enrichment, and staged-capture validation/cleanup.

## Dependencies

Uses local project/process protocols, the bridge/error boundary, operation waiting, and the package version. It is contract-coupled to editor command registration, shared limits/errors, state payloads, runtime gameplay, and project workflows.

## Invariants

- One typed specification owns each public tool.
- Modes are strictly nested (`tiny` ⊂ `small` ⊂ `large`) with stable ordering.
- Every argument object is validated before any local or bridge collaborator is called.
- Python-only wait fields are stripped before bridge dispatch.
- Project paths are preflighted consistently with editor-side confinement.
- Capture paths are derived from IDs, not accepted from the model; PNG bytes and dimensions are independently bounded before encoding and deletion.
- Expected command names, error codes, limits, protocols, and feature flags match live capabilities.

## Change and verification guide

Any tool addition begins here and must identify its mode, schema, handler kind, bridge command, path fields, wait policy, result contract, limits, and error behavior. Update matching Godot ownership and capability reporting in the same change. Run `tests.test_contracts`, `tests.test_schema_validation`, and `tests.test_server`; use the live integration test for bridge-contract changes.
