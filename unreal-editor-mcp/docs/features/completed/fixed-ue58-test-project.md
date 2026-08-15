---
feature_id: fixed-ue58-test-project
status: completed
depends_on:
  - python-tooling-decomposition
released_in: null
release_track: support-tooling
---

# `fixed-ue58-test-project` — Fixed UE 5.8 test-project path

**Outcome:** Headless integration always targets the disposable Unreal Engine 5.8 project in the current application checkout without requiring or accepting a project-path environment variable.

**Implementation status:** Completed as support tooling. It does not change Unreal MCP or companion semantic versions, runtime behavior, schemas, capabilities, or native plugin behavior.

**Depends on:**

- [`python-tooling-decomposition`](python-tooling-decomposition.md)

## Contract

- Resolve exactly `ue-test/ue58/UnrealMCPTest.uproject` relative to the `unreal-editor-mcp` application root derived from the runner source location.
- Do not depend on the caller's current working directory and do not accept an environment-variable or command-line project override.
- Fail before editor resolution or launch when the fixed descriptor is missing.
- Continue to read the Unreal Engine 5.8+ installation root from `UE58`; macOS continues to require the pinned `XCODE26_1_1` application path.
- Keep the project disposable, ignored, and isolated from personal Unreal projects.

## Verification

- Cover successful fixed-path resolution and a stable missing-descriptor failure with temporary application roots.
- Verify the runner reads only `UE58` through the required environment-path boundary and obtains the project from the fixed resolver.
- Run the focused headless support-tool tests, the complete Python suite, and documentation lint. Native builds, Automation, packaging, and production-socket execution are not required because editor commands, plugin code, and runtime behavior are unchanged.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
