# Repository Guidelines

## Purpose and Operating Environment

This repository contains lightweight tools for local LLM workflows, primarily MCP servers for LM Studio. Development and normal operation must work without ChatGPT or internet access. The target is a 16 GB MacBook Pro running small local models, so minimize memory use, startup time, dependencies, and context consumption.

## Core Design Constraints

- Prefer offline-capable, standard-library implementations.
- Do not require cloud services, telemetry, accounts, or runtime downloads.
- Keep processes small enough to run alongside LM Studio.
- Avoid background services when a short-lived or stdio process is sufficient.
- Prioritize reliable macOS behavior while using portable code where practical.
- Pin unavoidable dependencies and document how to prepare them before going offline.

## MCP Design for Small Models

Tool definitions and results consume the model's context. Small models also have weaker tool-selection and argument accuracy. Expose a small, focused tool set with short, distinct names, brief descriptions, simple schemas, and few arguments. Avoid redundant tools and long instructions. Return concise, predictable results with clear errors and bounded output. Prefer identifiers and paths relative to a configured root; do not expose long absolute paths to the model.

MCP stdio servers must write only protocol messages to stdout. Send diagnostics to stderr. Preserve LM Studio compatibility and test initialization, `tools/list`, and `tools/call` end to end.

## Repository Organization

Each application belongs in its own top-level folder with its source, tests, README, and configuration examples. Current application:

- `rooted-files-mcp/`: root-confined, text-only filesystem MCP server.

Keep shared documentation at the workspace root. Introduce shared libraries only when multiple applications need the same behavior.

## Development and Testing

Document run and test commands in each application's README. Prefer built-in test frameworks and fast offline suites. Test normal behavior, invalid input, resource limits, and security boundaries. Run the complete suite after behavior changes.

## Security and Resource Limits

Treat model-generated arguments as untrusted. Validate types, lengths, paths, and operations. Deny access outside configured roots, including traversal and symlink escapes. Bound recursion, file sizes, response sizes, and execution time. Avoid loading large files or datasets entirely into memory. Never commit secrets, personal paths, or machine-specific tokens.

## Contributions

Use focused commits with imperative subjects, for example `Add bounded search results`. Pull requests should describe behavior, memory or context impact, dependencies, security implications, and tests.
