# Configuration and effective policy

## Purpose

Load the fixed workspace configuration safely, validate its exact schema and bounds, resolve confined paths, apply CLI precedence, detect filesystem case behavior, and produce immutable effective settings.

## Owned source

- `rooted_files_mcp/configuration.py`.

## Dependencies

This is the lowest-level internal component and has no project-source dependency. Entry/composition, root confinement, text access, and tool filtering consume `Settings` or `ConfigurationError`. It is contract-coupled to README INI/CLI documentation and tests.

## Invariants

- Configuration is read only from `.mcp/rooted-files-mcp.ini` inside the resolved workspace.
- The configuration path must resolve inside the workspace to a regular UTF-8 file no larger than 64 KiB and containing no NUL bytes.
- Sections and keys are closed-world; duplicates, defaults, malformed booleans, and unknown values fail startup.
- An INI root must resolve inside the workspace; an explicit CLI root is a trusted override and may be elsewhere.
- Effective settings are frozen and record workspace, root, permissions, hidden policy, allowlist, and native case sensitivity.
- `.mcp` is protected and cannot be allowlisted. Allowlist names are exact single components, bounded, unique under native case rules, and additive to built-ins.

## Known pressure

Security invariants intentionally span this component and filesystem enforcement. Changes to protected names, case sensitivity, roots, permissions, or hidden allowlists require both configuration and filesystem tests.

## Change and verification guide

Update the matching type/library references and README workspace configuration. Run all configuration tests plus hidden/path/permission filesystem tests and subprocess startup tests.
