# Unreal Editor MCP Guidelines

Start at `docs/index.md` and follow `docs/workflow.md`. Use `README.md` for user guidance; executable contracts and runtime `capabilities` remain authoritative.

- Target Unreal Engine 5.8+ and Blueprint game-logic authoring.
- Use ignored `ue-test/` only as a disposable build and integration-test project; never treat its generated state as a committed contract.
- Build changes from small typed mutations. Prevalidate targets, types, limits, and stale state; use editor transactions where supported; expose bounded compile, save, and read-back results; preserve unrelated Blueprint content.
- Keep the C++ bridge localhost-only and authenticate every request with a durable, high-entropy per-project token. Fail closed on credential errors and never expose the token through discovery or heartbeat data.
- Isolate and test platform-specific discovery, paths, plugin loading, and process behavior. Validate editor changes with Unreal Automation, headless/command-line, and cross-process tests as applicable.
- Keep the `ROADMAP.md` native platform test backlog current whenever a feature is completed or native test evidence changes. Under every listed platform, include each completed feature that still lacks its applicable native verification, remove it only after that verification passes, and write `None` when the platform has no outstanding completed features.
