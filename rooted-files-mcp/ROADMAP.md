# Rooted Files MCP Roadmap

## Goal

Add workspace configuration, hidden-path filtering, and bounded line-range
editing without weakening the server's root confinement, text-only policy,
atomic writes, small tool catalog, or standard-library-only runtime.

This roadmap is an implementation plan. The checkboxes describe work that has
not yet been implemented.

## Compatibility and scope decisions

- Preserve the current `server.py <root>` and `rooted-files-mcp <root>` launch
  forms. The positional root remains an explicit command-line override.
- Treat the workspace as the directory used to discover
  `.mcp/file-access.ini`. Use `--workspace` when supplied; otherwise use the
  positional root, or the current working directory when configuration-only
  startup is requested.
- Apply settings in this order: explicit command-line value, INI value,
  built-in default. Command-line option defaults must therefore use an
  "unspecified" sentinel rather than eagerly replacing INI values.
- Keep the current defaults when no configuration changes them: read and write
  access enabled, hidden paths visible, a 5 MiB text limit, and a 100-entry tree
  limit.
- Continue to expose one root. Multiple named roots would add an identifier to
  every tool call and increase tool-selection cost for small models; add them
  only as a separately designed feature.
- Use one-based, end-inclusive source line numbers: `[start_line, end_line]`.
  Line 1 is the first line, matching compiler diagnostics, editor line numbers,
  and ordinary Git diff hunk coordinates. Require
  `1 <= start_line <= end_line <= line_count`.
- Preserve the existing UTF-8, binary-file, size, traversal, symlink, and
  atomic-replacement guarantees for both whole-file and line-range tools.

## Proposed configuration contract

The default file is `<workspace>/.mcp/file-access.ini`:

```ini
[paths]
root = .

[permissions]
read = true
write = true

[features]
show_hidden = false
line_access = true
```

Configuration behavior:

- `paths.root` is required for configuration-only startup. Resolve a relative
  value against the workspace. Require an INI-configured root to remain inside
  the workspace so a writable project configuration cannot broaden access on
  the next server restart. An explicit CLI root remains trusted and may point
  anywhere selected by the user.
- `permissions.read` controls `list_dir`, `tree`, `read_text`, and
  `read_lines`. `permissions.write` controls `write_text` and `write_lines`.
- `features.show_hidden = false` omits hidden entries from listings and denies
  direct or indirect access through every file tool. The default is `true` for
  backward compatibility when the setting is absent.
- `features.line_access` controls whether `read_lines` and `write_lines` are
  exposed. Its default is `true` after the feature is released.
- Matching CLI boolean options must support explicit true and false values, for
  example `--show-hidden` and `--hide-hidden`, while retaining an unspecified
  state for precedence resolution.
- A missing default INI file is not an error when an explicit root is present.
  A malformed file, unknown setting, invalid boolean, inaccessible configured
  root, or configuration-only startup without a root fails at startup with a
  concise diagnostic on stderr.
- Parse with `configparser.ConfigParser(interpolation=None)` in strict mode.
  Read only a regular UTF-8 file at the fixed workspace-relative location and
  impose a small configuration-size limit. Do not add runtime dependencies.

## Phase 1 — Configuration foundation

### Implementation

- [ ] Add `rooted_files_mcp/configuration.py` with a frozen settings data class,
  INI loader, validation helpers, and deterministic precedence merging.
- [ ] Separate workspace discovery from exposed-root selection. Resolve both
  paths once during startup and retain normalized `Path` objects.
- [ ] Extend the CLI without breaking the positional-root form. Add
  `--workspace` and paired boolean overrides for read, write, hidden visibility,
  and line access.
- [ ] Reject unknown sections and keys so misspelled security settings cannot be
  silently ignored.
- [ ] Bound the INI file size, reject NUL bytes and invalid UTF-8, and reject a
  configuration file or configured root that escapes the workspace through
  traversal or symlinks.
- [ ] Pass an immutable effective settings object into the filesystem and MCP
  server instead of consulting configuration during individual tool calls.
- [ ] Build `tools/list` from the effective permissions and feature flags. Also
  reject direct calls to disabled tools so a client cannot bypass discovery.
- [ ] Keep stdout reserved for JSON-RPC; send startup/configuration diagnostics
  to stderr through the existing argument-parser error path.

### Tests

- [ ] Add `tests/test_configuration.py` for missing, valid, malformed,
  oversized, invalid-UTF-8, duplicate, unknown, and escaping configurations.
- [ ] Test every precedence layer, including explicit false CLI overrides.
- [ ] Test roots containing spaces and platform-native path syntax without
  changing model-facing relative paths.
- [ ] Extend server tests to verify disabled tools are both omitted and rejected.
- [ ] Verify the legacy positional-root launch path behaves as it does today.

### Completion gate

- Existing filesystem and MCP tests pass unchanged.
- Configuration-only and legacy CLI startup both pass end-to-end stdio tests.
- No configuration or diagnostic text is written to protocol stdout.

## Phase 2 — Hidden-path policy

### Implementation

- [ ] Add one centralized hidden-path policy used by resolution, listing,
  traversal, reads, and writes. A root-relative component is hidden when its
  name begins with `.`; the root aliases `.` and an empty relative path are not
  themselves hidden.
- [ ] When hidden access is disabled, reject a path if either the requested
  normalized path or its resolved in-root target contains a hidden component.
  This prevents a visible symlink from becoming an alias to hidden content.
- [ ] Filter hidden entries from `list_dir` before sorting and formatting.
- [ ] Prune hidden entries and folders from `tree` before counting against the
  100-entry limit. Do not reveal that filtered entries exist.
- [ ] Apply the same check to non-existent write targets and their existing
  parents before creating temporary files.
- [ ] Keep the configuration load separate from model-facing access. The server
  may read its INI once at startup, while later MCP calls to `.mcp` are denied
  whenever hidden access is disabled.
- [ ] Use one stable, non-revealing error such as `Hidden path access is denied`
  for all direct and symlink-mediated hidden access failures.

### Tests

- [ ] Cover hidden files, hidden folders, nested hidden components, dot-prefixed
  write targets, and ordinary names containing dots.
- [ ] Cover `list_dir` and `tree`, including pruning, sorting, empty output, and
  tree-limit accounting.
- [ ] Cover visible symlinks to hidden in-root targets and hidden symlinks to
  visible targets where symbolic links are available.
- [ ] Confirm traversal and out-of-root symlink failures remain denied whether
  hidden visibility is enabled or disabled.
- [ ] Confirm `.mcp/file-access.ini` cannot be read or overwritten through MCP
  while hidden access is disabled.

### Completion gate

- Every filesystem operation enforces the same hidden policy.
- Listing results and permission errors do not disclose filtered names.
- Root confinement and current binary/text protections remain green.

## Phase 3 — Granular line access

### Tool contracts

Add two tools only when line access and the relevant permission are enabled:

```text
read_lines(path, start_line, end_line)
write_lines(path, start_line, end_line, content)
```

- Line numbers are integers, booleans are rejected as line numbers, and both
  bounds are inclusive. A compiler error at line 12 therefore maps directly to
  `start_line = 12, end_line = 12`.
- For a Git hunk such as `@@ -old_start,old_count +new_start,new_count @@`, the
  tool addresses the current file using the `+` side. When `new_count` is
  greater than zero, that hunk maps to `start_line = new_start` and
  `end_line = new_start + new_count - 1`. The comma value is a count, not an
  ending line number.
- A Git hunk with a zero line count describes a position between lines, not an
  addressable source line. The range tools do not accept line 0 or empty ranges;
  use `write_text` for an empty file and select an existing adjacent line when a
  replacement needs to add lines.
- `read_lines` returns only the selected text, without line-number decoration or
  newline normalization. Selected blank lines may therefore return only their
  newline characters.
- `write_lines` replaces complete logical lines. Unchanged prefix and suffix
  text retain their original bytes after UTF-8 decoding/encoding. New content
  uses the file's nearby newline convention, defaulting to `\n`, and preserves
  whether the original file ended with a newline when replacing through EOF.
- Empty content deletes the selected inclusive range. Replacement content may
  contain a different number of lines, so it can expand or contract the file.
  Empty files contain no addressable line range and must be initialized with
  `write_text`.
- Line numbers refer to the file state at the start of each call. A write that
  changes the number of lines makes later coordinates below that range stale;
  callers should re-read or adjust subsequent ranges.
- The resulting file, not only the replacement content, must remain within the
  5 MiB text limit.

### Implementation

- [ ] Refactor common name, size, signature, NUL, UTF-8, and hidden-path checks
  so whole-file and line-range methods cannot drift apart.
- [ ] Add a bounded line scanner that validates the entire file as UTF-8 text
  while retaining only the requested read range. Do not return the rest of the
  file to the model.
- [ ] Define line splitting for `\n`, `\r\n`, and final lines without a
  terminator. Preserve a UTF-8 BOM consistently with `read_text` behavior.
- [ ] Implement `RootedFilesystem.read_lines` with strict type and range checks.
- [ ] Implement `RootedFilesystem.write_lines` by producing a complete validated
  replacement and reusing the existing same-directory temporary file,
  `fsync`, permission preservation, and `os.replace` path.
- [ ] Revalidate the target immediately before replacement so symlink and parent
  checks are not skipped by the new write path.
- [ ] Return a concise write summary containing the replaced range and resulting
  UTF-8 byte count.
- [ ] Add compact MCP schemas and dispatch branches. Keep descriptions explicit
  about one-based, end-inclusive line numbers, and measure the resulting compact
  `tools/list` size for the README.

### Tests

- [ ] Cover first, middle, last, one-line, all-line, expansion, contraction, and
  deletion ranges.
- [ ] Cover `\n`, `\r\n`, mixed endings, a missing final newline, a final
  newline, an empty file, a UTF-8 BOM, and multibyte text.
- [ ] Reject zero, negative, reversed, non-integer, boolean, and out-of-bounds
  line numbers with stable messages.
- [ ] Test direct mapping from compiler line numbers and Git `+start,count` hunk
  coordinates, including omitted counts (which mean one line) and zero-count
  hunks (which are not addressable ranges).
- [ ] Reject missing files, folders, known binary extensions, binary signatures,
  NUL bytes, invalid UTF-8, oversized source files, and oversized results.
- [ ] Apply traversal, symlink, hidden-path, read-permission, and write-permission
  cases to both new tools.
- [ ] Simulate a replacement failure and confirm the original file and mode are
  preserved and the temporary file is cleaned up.
- [ ] Extend MCP tests for schemas, successful calls, missing arguments, disabled
  line access, and tool-result errors.

### Completion gate

- Range operations have identical security and text classification behavior to
  `read_text` and `write_text`.
- Writes are atomic on macOS, Linux, and Windows using the existing replacement
  strategy.
- Tool results remain bounded and schemas remain concise enough for small local
  models.

## Phase 4 — Documentation and release verification

- [ ] Update `rooted-files-mcp/README.md` with the INI schema, discovery and
  precedence rules, CLI examples, hidden-path semantics, permissions, line
  indexing, newline behavior, and configuration-only startup.
- [ ] Add macOS/Linux shell and Windows PowerShell examples using only portable
  path rules and the standard library.
- [ ] Update the README tool table and context-cost estimate after measuring the
  final compact `tools/list` response.
- [ ] Document safe offline preparation and confirm there are no new runtime
  dependencies or downloads.
- [ ] Update the root README if launch behavior or the project summary changes.
- [ ] Update `AGENTS.md` only if implementation changes repository-wide guidance,
  supported behavior, constraints, or known issues.
- [ ] Run `python3 -m unittest discover -s tests -v` from
  `rooted-files-mcp/` and record the verified platform.
- [ ] Exercise JSON-RPC `initialize`, `tools/list`, successful `tools/call`, and
  denied `tools/call` through the stdio launcher.
- [ ] Perform native Windows and Linux validation when those environments are
  available; do not claim them as verified based only on branch coverage.

## Planned file changes

| File | Planned responsibility |
|---|---|
| `rooted_files_mcp/configuration.py` | INI loading, validation, defaults, and precedence |
| `rooted_files_mcp/filesystem.py` | Effective access policy, hidden checks, and line operations |
| `rooted_files_mcp/server.py` | CLI options, dynamic tool catalog, and dispatch |
| `tests/test_configuration.py` | Configuration and precedence coverage |
| `tests/test_filesystem.py` | Hidden-path and line-range behavior |
| `tests/test_server.py` | Tool exposure, schemas, dispatch, and stdio behavior |
| `README.md` | User-facing setup, contracts, limits, and examples |

## Definition of done

- All three requested feature areas are implemented with standard-library code.
- CLI-over-INI precedence is deterministic and covered by tests.
- Disabled permissions and hidden paths are enforced at dispatch and filesystem
  boundaries, not only hidden from tool discovery.
- Whole-file and line-range operations share root, symlink, text, size, and
  atomic-write protections.
- The full offline test suite and stdio MCP smoke checks pass.
- All affected documentation and examples match the shipped behavior.
