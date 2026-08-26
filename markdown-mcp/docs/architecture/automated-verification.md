# Automated verification

## Purpose

Protect syntax, security, resource, concurrency, MCP, release, and native
platform contracts with dependency-free tests.

## Owned source

- `tests/test_configuration.py` — startup settings.
- `tests/test_mcp_config_generator.py` — host configuration rendering and
  generated process launches.
- `tests/test_paths.py` — path and fragment authority.
- `tests/test_markdown.py` — pure parser and transformations.
- `tests/test_filesystem.py` — bytes, limits, edits, races, and cleanup.
- `tests/test_server.py` — schemas and in-process MCP dispatch.
- `tests/test_subprocess.py` — real stdio framing and inherited-encoding behavior.

## Test boundaries

Unit tests isolate pure behavior and simulated races. Filesystem tests use
temporary roots and never create model-visible files. Subprocess tests launch
the repository entry script and communicate only through newline-delimited
JSON-RPC. Native platform tests exercise platform resolution behavior; on
Windows, junction escape coverage remains active when symbolic-link privileges
are unavailable.

## Validation sequence

Run focused affected modules first, then from `markdown-mcp/` run:

```sh
python -m unittest discover -s tests -v
```

After documentation changes, run the repository documentation linter from the
workspace root. Native macOS and Linux execution remains tracked separately in
the roadmap.
