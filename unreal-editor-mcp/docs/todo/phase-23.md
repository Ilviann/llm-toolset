# Phase 23 — Optional configured editor launch

**Outcome:** Agents can opt in to launching only the configured Unreal project/editor instance and wait for its exact authenticated bridge.

### Implementation

- Add the single `editor_lifecycle` tool only in opt-in large mode with a typed `launch` operation. Keep normal state reporting in the existing editor-state surface.
- Accept no executable path, project path, environment variable, or arbitrary process argument from the model. Configure and validate absolute editor and `.uproject` paths at MCP startup and expose only bounded availability information.
- Launch one detached configured editor instance through narrow platform adapters.
- Detect the exact project-specific authenticated bridge and distinguish `starting`, `ready`, `already_running`, cancelled, timed out, and failed startup.
- Bound concurrent launches, startup duration, retained results, discovery work, diagnostics, and child-process cleanup.

### Verification

- Test missing and malformed configuration, paths with spaces, another project or process on the port, repeated launches, version mismatch, timeout, cancellation, and abnormal startup.
- Run launch, readiness, cancellation, and recovery natively on macOS and Windows. Unit test Linux command construction without claiming native support.
- Prove the model cannot substitute executables, projects, environment values, shell fragments, or arbitrary arguments.

### Documentation and completion gate

- Document opt-in launch configuration, platform paths, states, cancellation, recovery, limits, and default-mode exclusion.
- Complete the phase only when configured launch reaches the exact authenticated bridge without arbitrary process execution on native macOS and Windows.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
