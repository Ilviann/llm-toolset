---
feature_id: offline-capabilities
status: completed
depends_on: []
released_in: "0.27.0"
---

# `offline-capabilities` — Offline project identity in capabilities

**Outcome:** Agents can identify the configured Unreal project through `capabilities` before Unreal Editor or its native bridge is active.

**Depends on:** None.

**Implementation status:** Completed in 0.27.0. The behavior is Python-owned and requires no native platform backlog entry.

### Implementation

- `ProjectLayout` derives an immutable non-path identity from the `.uproject` filename stem and the existing platform-normalized project hash.
- The CLI injects that identity into the MCP server. Every `capabilities` success reports it with Python and negotiated-protocol metadata; the active [`readonly-mode`](../active/readonly-mode.md) work reports authoritative access and independent lifecycle metadata.
- An `editor_unavailable` native call returns an explicit partial success with `bridge_ready: false` and `native_capabilities_available: false`. Native version, commands, features, limits, listener, asset-access, Blueprint-family, and version-match fields remain absent rather than being guessed.
- Live capability calls retain the complete authenticated native response, add configured project identity, and set `native_capabilities_available: true`.
- Authentication, invalid configuration, timeout, cancellation, version, and invalid-response errors remain model-facing errors.

### Verification

- Python project tests cover descriptor-stem names containing spaces and platform-normalized project hashes.
- MCP stdio tests cover live composition, editor-unavailable partial results, omission of native-only fields, and preservation of non-availability errors.
- Release-contract tests keep Python package and native plugin versions synchronized.

### Documentation and completion gate

- The [Python MCP server architecture](../../architecture/python-mcp-server.md), [Python wire contracts](../../types/python/contracts.md#python-wire-contracts), [editor bridge contracts](../../types/editor-bridge/contracts.md#native-wire-contracts), README, setup guide, and tool guide document local versus native field ownership.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
