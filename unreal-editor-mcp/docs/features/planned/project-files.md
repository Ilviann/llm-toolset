---
feature_id: project-files
status: planned
depends_on:
  - editor-restart
released_in: null
---

# `project-files` — Optional editor-offline project-file generation

**Outcome:** Agents can opt in to narrowly configured project-file generation only while the configured project editor is stopped.

**Depends on:**

- [`editor-restart`](../completed/editor-restart.md)

### Implementation

- Keep C++ source editing outside this application. Pair with `rooted-files-mcp` for separately configured confined text edits.
- Add the single `project_build` tool through an independent startup opt-in with a typed `generate_project_files` operation; do not couple it to project-content access or editor lifecycle enablement.
- Resolve Unreal Build Tool or platform scripts from validated startup configuration and the installed engine layout. Use fixed templates owned by narrow platform adapters.
- Accept no executable path, project path, shell fragment, environment variable, compiler flag, linker flag, working directory, or arbitrary argument from a tool call.
- Refuse generation while the authenticated configured editor is running or its lifecycle state is uncertain. Reconcile with durable lifecycle operations before starting.
- Bound process count, queueing, duration, output capture, diagnostic count and size, retained operation results, cancellation escalation, and child-process cleanup.
- Keep subprocess output off MCP stdout except inside valid bounded tool results.

### Verification

- Test fixed command construction, paths with spaces, missing tools, editor-running and uncertain-state rejection, timeout, cancellation, nonzero exit, oversized logs, retained-result replay, and process-tree cleanup.
- Run native offline project generation from packaged configuration on Windows without network access or runtime downloads. Repeat on macOS when available as non-blocking follow-up; no Linux verification is required.
- Prove that tool arguments cannot alter the executable, project, environment, working directory, command template, or fixed arguments.

### Documentation and completion gate

- Document offline engine/tool preparation, lifecycle interaction, bounded output, cancellation, platform behavior, default-mode exclusion, and use with the confined file MCP.
- Complete the feature only when fixed native project generation is reproducible from clean documented configuration on Windows. macOS reproduction may follow the release.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
