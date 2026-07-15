# Rooted Files MCP code structure, complexity, and coupling

## Scope and method

This document covers all hand-written Python source under `rooted-files-mcp/`, including production code and tests. It excludes package metadata, roadmap and README prose because they are not source code. Configuration metadata is still mentioned where code depends on entry points, versions, or fixed paths.

Dependencies below include:

- **direct dependencies**: Python imports and explicitly supplied collaborators;
- **runtime dependencies**: Python standard-library facilities and operating-system behavior used by a file;
- **contract dependencies**: schemas, settings, error text, security invariants, and metadata that must remain synchronized even without an import relationship.

The implementation contains **6 production source files**, totaling about **1,164 lines**, and **3 test source files**, totaling about **1,061 lines**. These counts include comments and blank lines and are structural indicators rather than cyclomatic-complexity measurements.

## Implemented modules

The word *module* in this section means a cohesive architectural subsystem. A source file can participate in more than one module when it currently combines responsibilities.

### M1. Process entry and composition

**Responsibility:** Provide script, package-module, and installed-command entry points; parse CLI settings; compose resolved configuration, the rooted filesystem, MCP request handling, and stdio serving.

**Files:** `server.py`, `rooted_files_mcp/__init__.py`, `rooted_files_mcp/__main__.py`, and the CLI/run portions of `rooted_files_mcp/server.py`.

**Dependencies on other modules:** M2 for effective settings; M3/M4 for filesystem behavior; M5 for MCP and stdio handling.

### M2. Configuration and effective policy

**Responsibility:** Load the fixed workspace INI file, enforce its size/location/schema, resolve workspace and root folders, merge CLI precedence, detect filesystem case sensitivity, validate the hidden allowlist, and produce immutable effective settings.

**Files:** `rooted_files_mcp/configuration.py`.

**Dependencies on other modules:** None. M1, M3, M4, and M5 consume its `Settings` or errors. Contract-coupled to `README.md` and tests through INI sections, keys, defaults, CLI precedence, and the protected `.mcp` name.

### M3. Root confinement and visibility policy

**Responsibility:** Enforce read/write permissions; reject absolute paths, traversal, root escapes, symlink escapes, protected configuration paths, and disallowed hidden names/Windows hidden attributes; list directories and bounded trees without following directory symlinks.

**Files:** the `HiddenPathPolicy`, path resolution, permission, listing, and tree portions of `rooted_files_mcp/filesystem.py`.

**Dependencies on other modules:** M2 for `Settings`, `ConfigurationError`, and `PROTECTED_NAMES`. M4 depends on this module for every path and permission decision; M5 exposes its operations.

### M4. Text validation and atomic editing

**Responsibility:** Reject binary names/content and non-UTF-8 or oversized files; read whole files or validated one-based line ranges; preserve BOMs and nearby line-ending conventions; atomically create/replace full files or line ranges; preserve modes and revalidate targets before replacement.

**Files:** the text scanning, decoding, reading, and writing portions of `rooted_files_mcp/filesystem.py`.

**Dependencies on other modules:** M2 for effective settings and M3 for permissions, path confinement, hidden policy, and write-target validation. M5 dispatches model-facing tools to these operations.

### M5. MCP API, JSON-RPC, and stdio transport

**Responsibility:** Define all tool schemas, filter tools by effective permissions, negotiate MCP protocol versions, validate JSON-RPC requests, dispatch filesystem calls, encode MCP tool errors, keep stdout protocol-clean, and report unexpected failures to stderr.

**Files:** the catalog, `MCPServer`, and stdio loop portions of `rooted_files_mcp/server.py`.

**Dependencies on other modules:** M1 for the package version and process composition; M2 for settings and startup errors; M3/M4 through `RootedFilesystem` and `FileAccessError`.

### M6. Automated verification

**Responsibility:** Verify configuration precedence and confinement, hidden-path policy across POSIX/Windows branches, text classification and line editing, atomic-write safeguards, MCP schemas/permissions/errors, and real subprocess stdio startup behavior.

**Files:** `tests/test_configuration.py`, `tests/test_filesystem.py`, `tests/test_server.py`.

**Dependencies on other modules:** Directly tests M1-M5. `test_server.py` also depends on the top-level launcher and package metadata contract through subprocess startup and version assertions.

## Complexity and coupling assessment

### Main complexity concentrations

| Area | Indicator | Assessment |
| --- | ---: | --- |
| `filesystem.py` | 599 lines | The dominant complexity and context hotspot. It combines visibility policy, path resolution, traversal, binary detection, UTF-8 decoding, line scanning, newline/BOM preservation, TOCTOU revalidation, and atomic writes. Security cohesion is strong, but the file contains two separable domains. |
| `configuration.py` | 292 lines | Moderate branching driven by strict schema validation, path confinement, platform case behavior, allowlist normalization, and CLI/INI/default precedence. It is cohesive and has no internal source dependency. |
| `server.py` | 256 lines | Combines static tool schemas, permission filtering, JSON-RPC handling, tool dispatch, stdio transport, and CLI composition. Each part is small, but combined responsibilities enlarge the context needed for protocol-only or CLI-only changes. |
| `test_filesystem.py` | 521 lines | Large because it covers the security matrix and line-editing edge cases. Its size mirrors the broad responsibility of `filesystem.py` and provides strong regression protection. |
| `test_server.py` | 318 lines | Mixes unit-level request tests and subprocess startup/configuration integration tests. The two groups are logically distinct despite sharing a file. |

### Coupling hotspots and change risks

1. **Security invariants span `configuration.py` and `filesystem.py`.** The protected `.mcp` name, hidden allowlist, case sensitivity, resolved roots, and effective permissions originate in configuration and are enforced in filesystem operations. Changes require both configuration and filesystem security tests.
2. **`filesystem.py` has high internal responsibility coupling.** All public operations share the same authoritative resolver and hidden policy, which is desirable. Text scanning and atomic replacement then add a second substantial concern, making unrelated changes load a large context.
3. **Hidden-path checks intentionally examine both requested and resolved paths.** This prevents a visible symlink alias from exposing a hidden target. Simplifying either side of the check would weaken policy.
4. **Writes depend on repeated validation.** `_write_target` validates before creating a temporary file, while `_replace_atomically` resolves and validates again before `os.replace`. This is deliberate TOCTOU protection and must be preserved through refactoring.
5. **Permission policy is represented twice.** `build_tools` hides disabled MCP tools, while `RootedFilesystem._require_read/_require_write` enforce permissions at execution time. Both layers are necessary defense in depth and must stay synchronized.
6. **Tool schemas and dispatch are colocated but manually synchronized.** The `TOOLS`, `READ_TOOLS`, and `WRITE_TOOLS` constants must match `_call_tool` names and arguments. Tests cover the current catalog and removed line-tool names.
7. **Text classification is shared by all read and write paths.** Extension, magic-byte, NUL, UTF-8, and size rules must remain consistent for whole-file and line-range operations. Existing-file writes intentionally read/validate the old file even in write-only mode.
8. **Line replacement has format-preservation coupling.** `_LineScan`, `_scan_text_lines`, `_replacement_bytes`, and `write_lines` jointly preserve BOMs, selected/nearby newline styles, final newline state, one-based coordinates, and size limits.
9. **Versioning is a small manual contract.** `rooted_files_mcp/__init__.py`, `pyproject.toml`, server initialization results, tests, README, and history must carry the same application version. The current source and package version is `0.3.0`.
10. **No third-party runtime dependency is present.** Production code uses only the Python standard library, keeping deployment and offline coupling low.

### Refactoring and optimization recommendations

#### R1. Separate path policy from text I/O behind the existing facade

**Recommendation:** Split `filesystem.py` into a low-level path/visibility policy module and a text-file module while retaining `RootedFilesystem` as the stable public facade. For example, keep `HiddenPathPolicy`, permission checks, `resolve`, `list_dir`, and `tree` in a rooted-path module; move `_LineScan`, binary/UTF-8 validation, line scanning, and atomic replacement into a text-access collaborator that can only operate on paths approved by the rooted policy.

**Benefit:** Materially improves context isolation, makes security review more focused, reduces the largest production file, and lets line-editing changes avoid loading platform hidden-file logic.

**Risk/tradeoff:** The split must not create an alternate path into text operations. Preserve a single authoritative resolver, requested-plus-resolved hidden checks, permission enforcement, and the pre-replace revalidation. This should be implemented as one tested architectural phase rather than incremental helper movement without boundary tests.

#### R2. Separate MCP catalog, transport, and CLI composition

**Recommendation:** Once M3/M4 are cleanly separated, divide `rooted_files_mcp/server.py` into a data-only tool catalog, an MCP request/dispatch class, a stdio transport, and a CLI composition module, keeping compatibility exports if needed.

**Benefit:** Protocol schema changes, transport behavior, and startup configuration would each require a smaller context. This would also align the File MCP structure with the already separated Godot MCP Python server.

**Risk/tradeoff:** More small files slightly increase navigation overhead. Keep the tool set compact and avoid abstract frameworks or new runtime dependencies.

#### R3. Split unit and subprocess server tests when M5 is refactored

**Recommendation:** Move `MCPServerTests` and `StdioStartupTests` into separate test files alongside the M5 split.

**Benefit:** Preserves context isolation between fast protocol-unit tests and slower process/configuration integration tests.

**Risk/tradeoff:** This is organizational only and should accompany the production split rather than create churn independently.

### Overall assessment

The implementation is small, dependency-free, security-conscious, and strongly tested. Coupling between configuration, path policy, and filesystem operations is mostly purposeful because they enforce one security boundary. The clearest maintainability opportunity is to reduce **context coupling** inside `filesystem.py` and `server.py` without weakening their invariants or expanding the model-facing tool set.

## Source file inventory and dependencies

### Python production files

#### `rooted-files-mcp/server.py`

**Responsibility:** Direct script launcher for MCP hosts that accept a file path.

**Internal source dependencies:** `rooted_files_mcp/server.py` (`main`).

**Module dependencies:** M1. No direct standard-library dependency.

#### `rooted-files-mcp/rooted_files_mcp/__init__.py`

**Responsibility:** Define package identity and the authoritative runtime version (`0.3.0`).

**Internal source dependencies:** None.

**Module dependencies:** M1. Contract dependencies: `pyproject.toml`, initialization output in `rooted_files_mcp/server.py`, tests, README, and history.

#### `rooted-files-mcp/rooted_files_mcp/__main__.py`

**Responsibility:** Support `python -m rooted_files_mcp` by invoking the packaged server entry point.

**Internal source dependencies:** `rooted_files_mcp/server.py`.

**Module dependencies:** M1.

#### `rooted-files-mcp/rooted_files_mcp/configuration.py`

**Responsibility:** Validate and load `.mcp/rooted-files-mcp.ini`, resolve confined workspace/root folders, detect native case sensitivity, validate hidden-name additions, merge CLI/INI/default values, and return frozen settings.

**Internal source dependencies:** None.

**Module dependencies:** M2. Runtime dependencies: Python `configparser`, `os`, `stat`, `sys`, `dataclasses`, and `pathlib`. Consumed by `filesystem.py` and `server.py`. Contract dependencies: documented INI schema, CLI flags, the protected `.mcp` directory, and package tests.

#### `rooted-files-mcp/rooted_files_mcp/filesystem.py`

**Responsibility:** Implement the complete root-confined, permission-aware, hidden-policy-aware, text-only filesystem: resolution, listing/tree traversal, binary/UTF-8/size checks, full/ranged reads, and atomic full/ranged writes with format preservation.

**Internal source dependencies:** `configuration.py` (`ConfigurationError`, `PROTECTED_NAMES`, `Settings`).

**Module dependencies:** M3 and M4, with M2 policy input. Runtime dependencies: Python `os`, `stat`, `tempfile`, `dataclasses`, `pathlib`, and typing/callables. Consumed by `server.py`. Contract dependencies: tool argument/result behavior in M5 and security/error assertions across all tests.

#### `rooted-files-mcp/rooted_files_mcp/server.py`

**Responsibility:** Define tool schemas and permission subsets, implement MCP/JSON-RPC request handling and tool dispatch, serve newline-delimited stdio safely, parse CLI flags, load settings, and compose the filesystem service.

**Internal source dependencies:** `__init__.py` (`__version__`), `configuration.py` (`ConfigurationError`, `Settings`, `load_settings`), `filesystem.py` (`FileAccessError`, `RootedFilesystem`).

**Module dependencies:** M1 and M5 directly; M2-M4 through composition/dispatch. Runtime dependencies: Python `argparse`, `json`, `sys`, and typing. Contract dependencies: `pyproject.toml` entry point, MCP protocol/tool schemas, README CLI/configuration instructions, and `test_server.py` subprocess behavior.

### Python test files

#### `rooted-files-mcp/tests/test_configuration.py`

**Responsibility:** Verify legacy defaults, configuration-only startup, workspace/root resolution, CLI precedence, frozen settings, hidden allowlist normalization/rejection, strict INI schema, bounded UTF-8 configuration reads, and traversal/symlink confinement.

**Internal source dependencies:** `configuration.py` constants, `ConfigurationError`, and `load_settings`.

**Module dependencies:** M6 testing M2. Runtime dependencies: Python `os`, `tempfile`, `unittest`, `dataclasses.FrozenInstanceError`, and `pathlib`.

#### `rooted-files-mcp/tests/test_filesystem.py`

**Responsibility:** Verify listing/tree/read/write behavior; traversal, symlink, hidden/protected, binary, UTF-8, and size boundaries; case and Windows attribute behavior; line coordinates and format preservation; permissions; atomic revalidation; failure cleanup; and mode preservation.

**Internal source dependencies:** `filesystem.py` (`MAX_TEXT_BYTES`, `TREE_LIMIT`, `FileAccessError`, `HiddenPathPolicy`, `RootedFilesystem`); transitively exercises `configuration.py` through default/effective settings.

**Module dependencies:** M6 testing M2-M4. Runtime dependencies: Python `os`, `re`, `stat`, `tempfile`, `unittest`, `dataclasses.replace`, `pathlib`, `types.SimpleNamespace`, and mocking.

#### `rooted-files-mcp/tests/test_server.py`

**Responsibility:** Verify MCP initialization/catalog schemas, permission-filtered tools, tool error encoding, whole/ranged filesystem dispatch, notifications, legacy and configuration-only subprocess startup, stdout/stderr isolation, configuration protection, and rejected legacy CLI flags.

**Internal source dependencies:** `filesystem.py` (`FileAccessError`, `RootedFilesystem`), `rooted_files_mcp/server.py` (`MCPServer`), and the top-level `server.py` launcher in subprocess tests; transitively exercises `configuration.py`.

**Module dependencies:** M6 testing M1-M5. Runtime dependencies: Python `json`, `subprocess`, `sys`, `tempfile`, `unittest`, `dataclasses.replace`, and `pathlib`.

## Dependency direction summary

The intended production dependency direction is:

```text
entry points and composition (M1)
  -> configuration/effective policy (M2)
  -> MCP API and stdio (M5)
       -> rooted path/visibility policy (M3) -> M2
       -> text validation/editing (M4) -> M3, M2
```

There are no production import cycles. `configuration.py` is the lowest-level internal module. `filesystem.py` consumes immutable settings and exposes one facade to `server.py`; entry points depend inward on that composition. The current main limitation is that M3/M4 share one large file and M1/M5 share another, producing context coupling without creating import coupling.
