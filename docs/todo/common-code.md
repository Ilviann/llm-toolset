# Common Python MCP code

Status: deferred design work. No shared library has been implemented.

Snapshot recorded: 2026-07-21 02:56:48 MSK (UTC+0300)

- Repository HEAD: `ec8756d0cd2cef6dd45ea1d8b0160c6421c61c23`
- Rooted Files MCP: `0.3.0`
- Godot Editor MCP: `0.16.1`
- Unreal Editor MCP: `0.1.0`

## Context

The three MCP applications independently implement parts of the same Python
infrastructure:

- MCP initialization, ping, tool listing, and tool calls;
- newline-delimited JSON-RPC over stdio;
- JSON-RPC result and error envelopes;
- MCP text-result encoding;
- static JSON tool schemas;
- command-line, module, and script entry points.

Godot Editor MCP and Unreal Editor MCP additionally have closely related JSON
Schema validators, bounded domain-error handling, authenticated localhost editor
bridges, and project-scoped discovery. Rooted Files MCP implements the common
MCP and stdio responsibilities inside its larger `server.py` module.

The applications do not currently import runtime code from one another. Their
similar functionality is duplicated, and some behavior has already diverged.
For example, Unreal Editor MCP bounds incoming stdio message length, while the
other two servers currently consume complete input lines without that bound.
Supported MCP protocol versions and error-message truncation also differ.

## Recommendation

Maintain one canonical source for a deliberately small common library and
vendor generated copies into each application before committing or releasing.
Do not use symlinks or installation-time copying as the normal development or
deployment mechanism.

A possible layout is:

```text
common/
  mcp_core/
    schema_validation.py
    jsonrpc.py
    stdio.py

rooted-files-mcp/rooted_files_mcp/_vendor/mcp_core/
godot-editor-mcp/godot_editor_mcp/_vendor/mcp_core/
unreal-editor-mcp/unreal_editor_mcp/_vendor/mcp_core/

scripts/
  sync_mcp_core.py
```

The exact names are provisional. The important properties are one editable
source, application-private deployed copies, and independently usable source
trees and distributions.

### Development and release workflow

1. Edit only the canonical `common/mcp_core/` source.
2. Run a dependency-free synchronization command to update every vendored copy.
3. Mark vendored files clearly as generated and not directly editable.
4. Provide a read-only `--check` mode that fails when copies are absent, modified,
   or stale.
5. Run each application's tests against its vendored copy, which is the code it
   will deploy.
6. Run the synchronization check as part of repository release verification.
7. Build each application as a self-contained wheel or source distribution with
   no runtime dependency on the repository layout.

Synchronization and release checks must derive consistency from source files,
package metadata, hashes, and runtime behavior. This document must not be used
as executable input or as a test fixture.

Atomic replacement should be used when the synchronization command updates a
vendored tree. A failed update must not leave a partially synchronized package.
The command should report which applications changed and support deterministic
output so an unchanged run produces no diff.

## Why symlinks are not recommended

Development symlinks would reduce visible duplication, but they are a poor
cross-platform and packaging contract:

- Git and Windows symlink behavior depends on checkout configuration, Developer
  Mode, or elevated privileges.
- Archives, build tools, IDEs, and security scanners may preserve, dereference,
  or reject symlinks differently.
- A symlink can escape a package's build context and make a source checkout work
  while the built artifact is incomplete.
- Tests run through symlinks do not verify the physical layout shipped to users.
- Direct script-path launches would depend on the surrounding monorepo layout.

Installation-time copying has related problems: it requires a custom mutating
step, can retain stale files after upgrades, and bypasses normal package
ownership and uninstall behavior. A copy operation should therefore be a
controlled development/release action, not an end-user installation contract.

## Conventional package alternative

A separately versioned first-party package such as `llmtoolset-mcp-core` is the
cleanest conventional Python packaging design. Each application could declare
an exact dependency and offline releases could include a local wheelhouse:

```toml
dependencies = ["llmtoolset-mcp-core==0.1.0"]
```

```shell
python -m pip install --no-index --find-links ./wheelhouse godot-editor-mcp
```

This approach provides standard installation, upgrade, dependency, and removal
semantics. It should be preferred if deploying two wheels per application is
acceptable. Its costs for this repository are an additional package and release
sequence, dependency resolution, version compatibility management, and loss of
the current one-project/one-artifact deployment model. It also complicates
direct execution from an isolated source checkout.

Reconsider the separate-package approach if the common code grows into a stable,
independently meaningful API or if several more applications begin consuming it.

## Initial extraction boundary

The first common library should remain small and dependency-free. Suitable
initial responsibilities are:

- the supported JSON Schema subset and `SchemaValidationError`;
- JSON-RPC success and error envelope construction;
- text MCP result encoding and an extension point for other content types;
- bounded newline-delimited stdio parsing and writing;
- a minimal request-handler protocol and optional shutdown/close protocol.

Configuration that differs by application must be explicit rather than global.
Expected configuration points include:

- diagnostic/server name;
- maximum inbound message size;
- maximum JSON-RPC error-message length;
- result-content encoders, including Godot image results;
- shutdown behavior.

Protocol negotiation and the generic `initialize`, `ping`, `tools/list`, and
`tools/call` router may be extracted later, after the three applications agree
on their behavioral contract. Supported protocol versions and server metadata
should remain application-supplied.

## Responsibilities to keep application-local initially

Do not initially extract these merely because their designs are analogous:

- tool catalogs, mode/permission filtering, and dispatch;
- rooted filesystem and project-path confinement;
- configuration loading and CLI policy;
- Godot and Unreal discovery-record schemas;
- token-file ownership and validation;
- raw-socket and HTTP editor bridge implementations;
- project-specific error codes and details;
- Godot asset, waiting, capture, and editor-launch behavior;
- Unreal platform/process behavior.

These areas have different security boundaries, transports, lifecycle rules, or
release contracts. Premature abstraction would hide important differences and
increase coupling between independently versioned applications.

## Behavioral decisions required before implementation

Before extracting code, define and test one canonical contract for:

- maximum inbound stdio message size and oversized-line recovery;
- maximum outbound response size, if enforced at this layer;
- JSON-RPC validation of IDs, methods, params, and notifications;
- error-message bounding and diagnostic output;
- MCP text and image result serialization;
- cleanup when stdin reaches EOF or the host shuts down;
- supported JSON Schema vocabulary and invalid-schema behavior;
- preservation of each application's current public imports, where required.

Adopt the strictest existing safe behavior unless compatibility evidence requires
otherwise. Any intentional behavior change needs focused tests and the complete
affected application suites.

## Suggested implementation phases

1. Record the common behavioral contract and add equivalent characterization
   tests to all three applications.
2. Reconcile security-relevant drift, particularly bounded stdio input.
3. Extract and vendor the schema validator.
4. Extract JSON-RPC envelopes, result encoding, and bounded stdio serving.
5. Add deterministic synchronization and repository-level consistency checks.
6. Update each application's architecture/type documentation, README only where
   user-visible setup changes, package metadata, history, roadmap, and version in
   accordance with that application's release policy.
7. Evaluate a shared MCP router only after the smaller common components have
   remained stable.

At every phase, keep each application independently installable, runnable,
testable, and releasable. No application should require network access, a cloud
service, an account, telemetry, or a runtime download to obtain the common code.
