---
feature_id: pcg-graph-authoring
status: planned
depends_on:
  - pcg-graph-inspect
released_in: null
---

# `pcg-graph-authoring` — Procedural Content Generation graph authoring

**Outcome:** Agents can create, transactionally edit, save, and read back bounded Unreal PCG Graph assets through the optional `UnrealMCPPCG` companion when Unreal reports its PCG plugin effectively enabled for the configured project and inspection support is live.

**Depends on:**

- [`pcg-graph-inspect`](pcg-graph-inspect.md)

### Graph creation and mutation

- Reuse the independently versioned optional `UnrealMCPPCG` companion, its effective project-enablement and strict companion API requirements, foundation registry, shared authenticated bridge, Game-thread dispatch, operation ledger, errors, limits, packaging, and inspection capability established by `pcg-graph-inspect`. Never edit the project descriptor, install or enable PCG, or create another listener, credential, Python package, or extension API.
- Add PCG graph mutation capability only when Unreal reports the Engine plugin named `PCG` effectively enabled for the configured project, the editor has loaded it successfully, and the companion and prerequisite inspection capability are verified live. Reject every create or mutation request before asset loading when any required state is unavailable or stale.
- Create exact project-owned PCG Graph assets and support bounded transactional operations for nodes, settings, parameters, connections, subgraph references, comments, and positions. Require stable record identities plus the latest authoritative graph snapshot for every existing-content mutation.
- Resolve node types, settings, typed values, and pin compatibility through Unreal's live public PCG APIs and the prerequisite capability-advertised allowlist. Reject unknown settings, invalid connections, recursive subgraphs, unsafe asset references, custom HLSL, arbitrary Blueprint execution, and supplied code.
- Preserve unrelated nodes, settings, parameters, edges, graph metadata, referenced assets, and prior dirty state on success or rejection. Reuse editor transactions, explicit compilation where required by public PCG APIs, saving, postcondition inspection, replay handling, lost-response reconciliation, exact rollback after unexpected failure, and stable bounded errors.
- Keep graph execution and generation, PCG Component inspection or mutation, unrestricted generated-object editing, custom settings classes, and runtime gameplay changes outside this feature.

### Verification

- Test graph creation and every supported node and settings family, parameter operation, connection and disconnection, subgraph reference, comment, movement, removal, compilation, saving, restart, and exact structural read-back through the prerequisite inspection contract.
- Test incompatible pins, subgraph cycles, unsupported or unsafe settings, malformed typed values, invalid asset paths, limits, stale identities and snapshots, compile and save failure, transaction rollback, undo and redo, replay, timeouts, lost-response recovery, and unchanged-content fingerprints.
- Test missing, disabled, unloaded, stale, or excluded project `PCG` configuration plus absent, inspection-only, mismatched, disabled, stale, and unsupported companion states without partially registering or executing mutation handlers. Verify mutation capability registration and removal across editor restart and project-plugin state changes.
- Prove accepted and rejected mutations preserve unrelated graph content, referenced assets, package state, generated output, and base-plugin behavior. Run the complete base and PCG companion suites natively on Windows against the supported Unreal Engine build. Repeat on macOS when available as non-blocking follow-up.

### Documentation and completion gate

- Document the additional mutation capability, effective per-project `PCG` enablement prerequisite, Engine-default and project-override behavior, supported creation and graph-editing operations, typed values, identity and snapshot rules, compilation and saving, limits, exclusions, recovery, and a representative graph-authoring example. Link to the prerequisite inspection guide instead of repeating its read contract.
- Update companion installation, packaging, independent semantic-version, and companion API documentation only where mutation support changes them. State that Unreal MCP never installs or enables PCG or edits the project descriptor.
- Complete the feature only when a representative PCG Graph can be created, edited, saved, restarted, and read back exactly while unsupported operations fail closed, unrelated graph content remains unchanged, inspection-only states cannot expose mutation, and no PCG operation is available unless Unreal reports `PCG` effectively enabled for the configured project.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
