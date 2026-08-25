# Markdown MCP feature workflow

Apply these rules to every feature, fix, or refactor.

1. Trace startup mode, model-facing path splitting, requested and resolved
   suffixes, root confinement, file identity, strict UTF-8/BOM handling, parser
   spans, byte limits, and atomic write revalidation.
2. Keep parser transformations pure and source-exact. Keep all filesystem
   authority in `filesystem.py` and `paths.py`, and all model-facing schemas and
   dispatch in `server.py`.
3. Add proportional normal, invalid, limit, security, formatting, concurrency,
   MCP framing, and platform coverage. Run focused tests, then the full suite.
4. For runtime releases, synchronize Python/package versions, initialization
   output, tests, README, roadmap feature status, feature metadata, and history.
