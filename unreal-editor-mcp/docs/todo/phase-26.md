# Phase 26 — Optional editor-target builds

**Outcome:** Agents can opt in to narrowly configured editor-target builds only while the configured project editor is stopped.

### Implementation

- Extend `project_build` with a typed `build_editor_target` operation while keeping the tool in opt-in large mode.
- Let the model select only targets and configurations from a bounded published allowlist. Never accept executable paths, project paths, shell fragments, environment variables, compiler/linker flags, working directories, or arbitrary arguments.
- Reuse the Phase 25 fixed platform adapters, stopped-editor precondition, durable lifecycle reconciliation, process bounds, retention, cancellation escalation, and child-process cleanup.
- Normalize compiler diagnostics and keep raw subprocess output off MCP stdout except inside valid bounded tool results.

### Verification

- Test target and configuration allowlists, fixed command construction, missing tools, invalid selections, editor-running and uncertain-state rejection, timeout, cancellation, nonzero exit, oversized logs, normalized diagnostics, replay, and process-tree cleanup.
- Run native offline editor-target builds from packaged configuration on macOS and Windows without network access or runtime downloads. Unit test Linux construction separately.
- Prove that tool arguments cannot alter the executable, project, environment, working directory, command template, or unrestricted flags.

### Documentation and completion gate

- Document configured allowlists, offline tool preparation, lifecycle interaction, bounded diagnostics, cancellation, platform behavior, and default-mode exclusion.
- Complete the phase only when fixed native editor-target builds are reproducible from clean documented configuration on macOS and Windows.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
