---
feature_id: pcg-graph-inspect
status: planned
depends_on:
  - companion-plugins
released_in: null
---

# `pcg-graph-inspect` — Procedural Content Generation graph inspection

**Outcome:** Agents can discover and inspect existing bounded Unreal PCG Graph assets through an optional editor-only companion plugin, but only when Unreal reports its PCG plugin effectively enabled for the configured project.

**Depends on:**

- [`companion-plugins`](companion-plugins.md)

### Project enablement and companion contract

- Require Unreal's plugin manager to report the Engine plugin named `PCG` effectively enabled for the configured project and require its public modules to load successfully. Honor Engine defaults, explicit project enablement or disablement, target and platform restrictions, and load failures; installed files, another project's configuration, or stale prior capability state are not sufficient. Do not require a literal `.uproject` entry when the supported Engine distribution enables PCG by default.
- Never edit the project descriptor, install the Engine plugin, enable it, restart the editor, or infer a different plugin state on the user's behalf. When Unreal reports `PCG` disabled or unavailable for the configured project, publish PCG as unavailable with a stable reason and reject every PCG operation before asset discovery or loading.
- Add a separate editor-only `UnrealMCPPCG` companion plugin that owns all direct dependencies on the Engine's PCG plugin and only the public PCG and PCG editor modules required by compiled probes and native behavior.
- Keep `UnrealMCP` independent of PCG headers, modules, plugin enablement, and transitive Engine-plugin dependencies. The base plugin must build, package, load, and retain its complete non-PCG capabilities when `UnrealMCPPCG` is absent or the Engine PCG plugin is missing, disabled, or unloaded.
- Reuse the base-owned extension registry and companion lifecycle established by `companion-plugins` rather than adding another listener or credential. The companion may register only its PCG asset policy, typed inspection handlers, and bounded capability data; it must reuse base authentication, Game-thread dispatch, errors, limits, and shutdown.
- Version `UnrealMCPPCG` independently from `UnrealMCP` under the shared semantic-version policy. Require both descriptors to declare the same global `companion_api_version` and require the compiled base and companion constants to match each other and their descriptors before registration; do not pin the companion's required `UnrealMCP` plugin reference to the base semantic version.
- Extend release-contract tests to validate Python/base version pairing, each plugin's internal semantic-version sources, descriptor and compiled companion API values, and extension-schema compatibility. Publish companion semantic version, `companion_api_version`, readiness, effective project enablement, Engine-plugin load state, inspection support, supported graph settings families, and effective limits through `capabilities`; fail closed and expose no PCG inspection when any required state or compatibility value is absent or mismatched.

### Graph discovery and inspection

- Discover exact mounted PCG Graph assets and inspect graph settings, parameters, nodes, typed node settings, pins, edges, subgraph references, comments, positions, diagnostics, and a deterministic structural snapshot.
- Resolve node and settings types, pin schemas, typed values, and referenced assets through Unreal's live public PCG APIs and a capability-advertised allowlist. Return typed unsupported or unresolved records rather than using unrestricted reflection, executing the graph, loading unsafe references, or silently omitting unknown data.
- Give every graph, parameter, node, pin, edge, subgraph reference, and nested supported settings record a stable identity. Report ordering, inheritance or defaults where meaningful, truncation, and one authoritative graph snapshot for later stale-safe mutations.
- Bound graph discovery, node and edge counts, subgraph traversal and cycle detection, nested values, asset-reference resolution, diagnostics, execution time, response size, pagination, and retained state. Reject recursive or excessive subgraph traversal without mutating or normalizing any graph.
- Do not create, edit, compile, generate from, save, or execute PCG Graphs; expose custom HLSL, arbitrary Blueprint execution, supplied code, PCG Component mutation, or any model-facing mutation capability in this feature.

### Verification

- Test a project with no `PCG` reference against the supported Engine's enabled-by-default state, explicit `Enabled: false` and `Enabled: true` overrides, malformed or duplicate references, platform or target exclusions, and load failure; prove only Unreal's effective enabled and successfully loaded project state can register inspection.
- Test missing companion, missing Engine plugin, load failure, missing or mismatched descriptor API values, compiled API mismatch, descriptor-versus-compiled disagreement, stale binaries and capabilities, inconsistent companion-owned semantic-version sources, independently different valid semantic versions, unavailable public editor modules, and unsupported Unreal builds without partial handler registration. Prove the base plugin remains fully usable in every unavailable state.
- Inspect representative existing PCG Graphs covering every supported node and settings family, typed parameters, pins, edges, subgraphs, comments, positions, defaults, asset references, and unsupported records; restart and verify the same bounded structural read-back.
- Test invalid asset paths, incompatible or unresolved references, subgraph cycles, malformed values, stale pages and snapshots, duplicate identities, and discovery, traversal, time, and response limits.
- Fingerprint graph content, referenced assets, package dirtiness, generated output, and Undo history before and after every success and rejection. Prove inspection never executes, compiles, saves, normalizes, or changes a graph and that no PCG mutation operation is advertised or accepted.
- Run focused public-header probes, normal, forced-unity, and non-unity builds, native Automation, packaging and version-contract tests, and full cross-process inspection verification on Windows against the supported Unreal Engine build. Repeat on macOS when available as non-blocking follow-up.

### Documentation and completion gate

- Document the effective per-project `PCG` enablement prerequisite, Engine-default and project-override behavior, capability and unavailable-reason fields, companion installation, independent semantic versioning, strict `companion_api_version` equality, supported graph inspection records, identities, snapshots, pagination, limits, exclusions, and a representative read-only example.
- State that Unreal MCP never installs or enables PCG or edits the project descriptor. Document offline source and binary packaging of the base and companion from one release state.
- Complete the feature only when PCG inspection follows Unreal's effective configured-project plugin state, disabled or unavailable states cannot expose or execute it, the base plugin remains fully usable without PCG, representative existing PCG Graphs can be read after restart without execution or mutation, and the complete base and inspection suites pass on both native platforms.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
