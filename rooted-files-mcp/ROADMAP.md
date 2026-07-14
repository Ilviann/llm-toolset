# Rooted Files MCP Roadmap

## Goal

Add workspace configuration, hidden-path filtering, and bounded line-range
editing without weakening the server's root confinement, text-only policy,
atomic writes, small tool catalog, or standard-library-only runtime.

This roadmap is an implementation plan. Unchecked boxes describe work that has
not yet been implemented.

## Phase delivery contract

Each phase must leave the server in a working, releasable state and deliver a
complete feature or cohesive feature set. A phase may depend on completed
earlier phases, but its implementation, security and failure-path tests,
end-to-end MCP checks, and affected documentation and examples are all part of
that phase. Testing and documentation must not be deferred to a separate final
phase. A phase is complete only after the full existing test suite remains
green and its completion gate is satisfied.

## Compatibility and scope decisions

- Preserve the current `server.py <root>` and `rooted-files-mcp <root>` launch
  forms. The positional root remains an explicit command-line override.
- Treat the workspace as the directory used to discover
  `.mcp/rooted-files-mcp.ini`. Use `--workspace` when supplied; otherwise use the
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

The default file is `<workspace>/.mcp/rooted-files-mcp.ini`:

```ini
[paths]
root = .

[permissions]
read = true
write = true

[features]
show_hidden = false
hidden_allowlist =
    .editorconfig
    .github
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
  direct or indirect access through every file tool, except for exact names in
  the effective hidden allowlist. The built-in allowlist contains `.gitignore`
  and `.env.template`. `features.hidden_allowlist` adds names to that built-in
  set; it does not replace the built-ins. The multiline example above therefore
  also permits `.editorconfig` and `.github`. The default for `show_hidden` is
  `true` for backward compatibility when the setting is absent.
- Allowlist values are exact single path-component names, shared by files and
  folders. Put one name on each non-empty continuation line. Each name must
  not be `.`, `..`, contain a path separator or NUL, or match a protected name.
  Non-dot names are valid so Windows attribute-hidden entries such as
  `desktop.ini` can be allowed explicitly; on other platforms such a name has
  no special effect unless the entry is otherwise hidden. Trim surrounding
  configuration whitespace, reject duplicates, and bound both the entry count
  and entry length. The effective allowlist is immutable after startup.
- Maintain a separate built-in protected-name set, initially `{.mcp}`. A
  protected component is omitted and denied regardless of `show_hidden`, the
  allowlist, file type, or Windows attributes. It cannot be overridden by INI
  or CLI settings. This keeps `.mcp/rooted-files-mcp.ini` and any other content
  under `.mcp` inaccessible to model-facing tools. Protection applies to
  root-relative components; the explicitly selected root itself is still the
  trust boundary even when its own basename is `.mcp`.
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

- [x] Add `rooted_files_mcp/configuration.py` with a frozen settings data class,
  INI loader, validation helpers, and deterministic precedence merging.
- [x] Separate workspace discovery from exposed-root selection. Resolve both
  paths once during startup and retain normalized `Path` objects.
- [x] Extend the CLI without breaking the positional-root form. Add
  `--workspace` and paired boolean overrides for read, write, hidden visibility,
  and line access.
- [x] Reject unknown sections and keys so misspelled security settings cannot be
  silently ignored.
- [x] Bound the INI file size, reject NUL bytes and invalid UTF-8, and reject a
  configuration file or configured root that escapes the workspace through
  traversal or symlinks.
- [x] Pass an immutable effective settings object into the filesystem and MCP
  server instead of consulting configuration during individual tool calls.
- [x] Build `tools/list` from the effective permissions and feature flags. Also
  reject direct calls to disabled tools so a client cannot bypass discovery.
- [x] Keep stdout reserved for JSON-RPC; send startup/configuration diagnostics
  to stderr through the existing argument-parser error path.

### Tests

- [x] Add `tests/test_configuration.py` for missing, valid, malformed,
  oversized, invalid-UTF-8, duplicate, unknown, and escaping configurations.
- [x] Test every precedence layer, including explicit false CLI overrides.
- [x] Test roots containing spaces and platform-native path syntax without
  changing model-facing relative paths.
- [x] Extend server tests to verify disabled tools are both omitted and rejected.
- [x] Verify the legacy positional-root launch path behaves as it does today.

### Documentation

- [x] Document the INI schema, workspace and root discovery, precedence, CLI
  overrides, startup behavior, and portable launch examples in `README.md`.
- [x] Record the completed scope, tests, verified platform, and remaining
  native-platform validation limits in this roadmap.

### Completion gate

Completed on macOS on 2026-07-14. The full 30-test suite includes end-to-end
stdio coverage for legacy and configuration-only startup and confirms that
startup diagnostics do not enter protocol stdout.

- Existing filesystem and MCP tests pass unchanged.
- Configuration-only and legacy CLI startup both pass end-to-end stdio tests.
- No configuration or diagnostic text is written to protocol stdout.

## Phase 2 — Hidden-path policy

### Policy contract

Apply these rules in order to every root-relative path component:

1. If the component matches a protected name, deny it. The initial protected
   set contains only `.mcp`, and protection always wins.
2. Determine whether the component is hidden. On every platform, names that
   begin with `.` are hidden. On Windows, an existing file, directory, or
   symbolic-link/reparse-point entry with `FILE_ATTRIBUTE_HIDDEN` is also
   hidden, even when its name does not begin with `.`. The root aliases `.` and
   an empty relative path are not components and are not hidden.
3. If `show_hidden = true`, permit the component unless it is protected.
4. If `show_hidden = false`, permit a hidden component only when its exact name
   is in the effective allowlist. Permit ordinary non-hidden components.

The built-in allowlist is `{.gitignore, .env.template}`. Configured names extend
it. Matching is by a complete component, never a prefix, suffix, glob, or path:
`.gitignore` is permitted, `.git` remains denied, and `.env.template.local`
does not match `.env.template`. The same matching rule applies to files and
folders, so an allowlisted folder may be listed and traversed. Every nested
component is still evaluated independently.

Protected-name comparison must prevent case-only aliases on case-insensitive
filesystems. Allowlist comparison should use the actual on-disk component name,
not merely user-supplied casing, so behavior follows exact configured names
without allowing a differently cased request to bypass a protected component.
Reject configured names that collide with built-in or configured entries under
the relevant comparison rule.

`show_hidden` changes access, not only presentation. A denied entry is absent
from `list_dir` and `tree`, and direct reads and writes fail with the same
non-revealing `Hidden path access is denied` error whether the entry exists,
is protected, is dot-prefixed, has the Windows Hidden attribute, or is reached
through a symlink. The error must not identify which component triggered it.

### Implementation

- [x] Add one centralized hidden-path policy used by resolution, listing,
  traversal, reads, and writes. Keep protected names, built-in allowed names,
  configured allowed names, hidden detection, and matching in this one policy.
- [x] Extend strict INI parsing for `features.hidden_allowlist`. Parse one
  component per continuation line, validate and bound the set, merge it with
  the built-in allowlist, and reject protected-name collisions at startup.
- [x] On Windows, detect `stat.FILE_ATTRIBUTE_HIDDEN` through standard-library
  stat data for each existing lexical component and resolved target component.
  Isolate the platform-specific branch so POSIX does no extra attribute work
  and Windows behavior can be unit-tested without a Windows runtime.
- [x] When hidden access is disabled, reject a path if either the requested
  normalized path or its resolved in-root target contains a denied hidden
  component. Check protected components regardless of `show_hidden`. This
  prevents a visible symlink from becoming an alias to hidden or protected
  content, and prevents a denied hidden symlink from aliasing visible content.
- [x] Filter denied entries from `list_dir` before sorting and formatting, while
  retaining allowlisted hidden entries.
- [x] Prune denied entries and folders from `tree` before counting against the
  100-entry limit. Descend into allowlisted hidden folders, evaluate all of
  their children normally, and do not reveal that filtered entries exist.
- [x] Apply name-based checks to non-existent write targets and attribute checks
  to every existing parent before creating temporary files. Recheck the target
  and parent chain immediately before replacement so a symlink, rename, or
  Windows attribute change cannot skip the policy.
- [x] Keep the configuration load separate from model-facing access. The server
  may read its INI once at startup, while later MCP calls through a root-relative
  `.mcp` component are always denied.
- [x] Use the stable, non-revealing `Hidden path access is denied` error for all
  direct, protected, attribute-based, and symlink-mediated policy failures.

### Tests

- [x] Cover the built-in `.gitignore` and `.env.template` allowances; denial of
  `.git`, `.env`, and `.env.template.local`; configured additions; and the
  additive rather than replacement merge behavior.
- [x] Cover allowlisted names used as both files and folders, nested allowlisted
  folders, a denied hidden component below an allowed folder, dot-prefixed
  write targets, and ordinary names containing dots.
- [x] Cover invalid allowlist configuration: empty entries where applicable,
  `.`, `..`, both separator styles, NUL, duplicates, excessive entries or name
  length, and protected `.mcp` collisions.
- [x] Confirm `.mcp` is omitted and denied as a file, folder, nested component,
  direct target, and write target with both values of `show_hidden`, including
  when an allowlist entry attempts to differ only by case.
- [x] Cover `list_dir` and `tree`, including pruning, sorting, empty output, and
  tree-limit accounting. Confirm allowlisted entries count normally and denied
  entries do not consume the limit.
- [x] Cover visible symlinks to denied and allowlisted hidden in-root targets;
  denied and allowlisted hidden symlinks to visible targets; and symlinks into
  `.mcp` where symbolic links are available.
- [x] On Windows, cover attribute-hidden files, folders, and reparse-point
  entries with and without dot-prefixed names; allowlisted attribute-hidden
  names; inherited traversal through attribute-hidden folders; and an attribute
  change before atomic replacement. On non-Windows hosts, unit-test attribute
  classification with injected or mocked stat metadata.
- [x] Cover casing of protected and allowed names on case-sensitive and
  case-insensitive filesystems without weakening root confinement.
- [x] Confirm traversal and out-of-root symlink failures remain denied whether
  hidden visibility is enabled or disabled.
- [x] Confirm `.mcp/rooted-files-mcp.ini` cannot be listed, read, or overwritten
  through MCP even when hidden visibility is enabled.

### Documentation

- [x] Document protected names, hidden detection, allowlist behavior, direct
  access denial, platform-specific behavior, and security limits in
  `README.md`.
- [x] Record the completed scope, tests, verified platform, and remaining
  native-platform validation limits in this roadmap.

### Completion gate

Completed on macOS on 2026-07-14. The 45-test suite covers POSIX behavior,
simulated Windows Hidden-attribute metadata and attribute changes, native
case-sensitivity policy branches, symlink aliases, tree-limit accounting, and
end-to-end stdio protection of the workspace configuration. Native Linux and
Windows validation remains pending.

- Every filesystem operation enforces the same hidden policy.
- Protected-name precedence, allowlist exceptions, and Windows Hidden
  attributes behave identically across listing, traversal, reads, and writes.
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

### Documentation and release verification

- [ ] Update `README.md` with the line tools, one-based inclusive indexing, Git
  hunk mapping, newline behavior, permissions, limits, and examples.
- [ ] Update the README tool table and context-cost estimate after measuring the
  final compact `tools/list` response.
- [ ] Review the existing macOS/Linux shell and Windows PowerShell setup
  examples for the completed feature set and keep all paths portable.
- [ ] Document safe offline preparation and confirm that the phase adds no
  runtime dependency or download.
- [ ] Update the root README if the project summary or launch behavior changes,
  and update this roadmap and `AGENTS.md` when their documented guidance or
  known issues are affected.
- [ ] Run the complete offline test suite and record the verified platform.
- [ ] Exercise JSON-RPC `initialize`, `tools/list`, successful `tools/call`, and
  denied `tools/call` through the stdio launcher.
- [ ] Record native Windows and Linux results when those environments are
  tested; do not broaden verification claims from simulated branch coverage.

### Completion gate

- Range operations have identical security and text classification behavior to
  `read_text` and `write_text`.
- Writes are atomic on macOS, Linux, and Windows using the existing replacement
  strategy.
- Tool results remain bounded and schemas remain concise enough for small local
  models.
- The complete offline suite and stdio MCP smoke checks pass, affected
  documentation matches the shipped behavior, and the recorded platform claims
  match the validation actually performed.

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
