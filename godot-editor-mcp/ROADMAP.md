# Godot Editor MCP Roadmap

## Phase checklist

- [ ] Phase 13 — Harden bounded boundaries and reduce runtime coupling.

## Phase 13 — Harden bounded boundaries and reduce runtime coupling

**Target release:** 0.16.1

**Outcome:** Close the identified resource, overwrite, validation, startup, test,
and maintainability gaps without adding model-facing tools or changing the
runtime debugger protocol.

### Project-wide constraint

No production code, test code, generated code, or release check may depend on
the contents of documentation files. Documentation includes `README.md`,
`ROADMAP.md`, `HISTORY.md`, `CODE.md`, and other prose formats. Tests may verify
code, package metadata, plugin metadata, runtime-reported contracts, and
behavior, but documentation must remain an output of the development process,
not an executable input or test fixture.

### Work

1. **Bound localhost bridge clients.** Add a small explicit maximum for active
   bridge clients, record a monotonic deadline for each accepted connection,
   reject excess connections, and disconnect clients that do not authenticate
   and submit a complete request within the deadline. Keep debugger-deferred
   clients bounded by both the bridge-client limit and the existing runtime
   pending-request limit. Report the effective client and timeout limits through
   capabilities and the mirrored Python contract.

2. **Guarantee import no-overwrite behavior.** Replace the check-then-
   `os.replace` publication path with a portable atomic no-replace strategy for
   the completed same-directory temporary file. Preserve confinement, bounded
   streaming, durability, cleanup, and Windows/POSIX behavior. A destination
   created at any point before publication must remain unchanged and return a
   stable domain error.

3. **Enforce the published tool schemas.** Validate every MCP `tools/call`
   argument object from the existing `ToolSpec.inputSchema` before local or
   bridge dispatch, using a dependency-free implementation limited to the JSON
   Schema vocabulary already present in the catalog. Enforce required fields,
   types, enums/constants, numeric and collection bounds, object property
   limits, `additionalProperties`, `oneOf`, and the existing path pattern.
   Reject falsy non-object `params` and `arguments` instead of coercing them to
   empty objects. Retain editor-side validation as defense in depth and align
   error behavior across local and bridge-backed tools.

4. **Fail closed when authentication cannot be persisted.** Make token loading
   or creation return an explicit failure when the bounded token file cannot be
   read or written. Abort listener and discovery startup in that state so the
   plugin cannot advertise a bridge that legitimate clients cannot
   authenticate to.

5. **Split the runtime probe by responsibility.** Keep `runtime_probe.gd` as the
   debugger-autoload composition and protocol shell, and extract focused
   collaborators for runtime tree/property inspection and snapshots, capture
   staging, injected-input scheduling, and condition validation/evaluation.
   Centralize runtime node-path validation and pass a narrow shared runtime
   identity context rather than allowing the extracted services to own
   handshake state. Preserve the exact probe protocol version, commands,
   identity checks, limits, wire responses, inert-without-debugger behavior, and
   absence of networking or supplied-code evaluation in the running game.

6. **Repair release and architecture checks.** Remove all reads and assertions
   against documentation contents from `test_contracts.py` and any other code.
   Continue to verify release consistency from executable/package sources such
   as `pyproject.toml`, `plugin.cfg`, Python package identity, GDScript runtime
   identity, and live capabilities. Replace brittle source-text architecture or
   security assertions where practical with behavioral or collaborator-
   boundary tests; source checks may inspect source code, but never
   documentation.

### Verification and completion criteria

- Add bridge tests for excess connections, incomplete requests, expired idle
  clients, deferred responses, normal authenticated calls, and shutdown cleanup.
- Add deterministic import tests that create the destination immediately before
  publication and prove its original bytes are preserved on macOS/Linux and the
  mocked Windows branch.
- Add table-driven schema-validation tests covering every supported schema
  keyword, nested transaction/value forms, unknown fields, missing fields, and
  falsy non-object request values; retain end-to-end handler validation tests.
- Add token persistence-failure coverage proving that the bridge listener and
  discovery record are not started.
- Move existing runtime probe behavior tests to the extracted services, add
  focused lifecycle tests for the composition shell, and keep the live
  editor/game integration passing without a probe protocol change.
- Run the complete Python suite, all Godot Phase 2–13 headless checks, the
  headless plugin load, and the opt-in native macOS reload/runtime integration.
  Linux and Windows implementation branches must have deterministic automated
  coverage; native validation remains recorded separately when available.
- Update `CODE.md`, `README.md`, configuration examples, capability
  documentation, and `HISTORY.md` as outputs after implementation. Update all
  package, plugin, and runtime version sources consistently to 0.16.1.
- Complete the phase only when the normal test command passes with no roadmap or
  other documentation-content dependency and the working application remains
  releasable offline with no new runtime dependency.
