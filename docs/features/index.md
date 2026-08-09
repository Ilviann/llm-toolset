# Workspace feature documentation

## Status groups

- Planned: None.
- Active: None.
- [Completed features](completed/index.md).
- [Deferred features](deferred/index.md).

## Front matter contract

Every feature document begins with YAML front matter containing string `feature_id`, enum `status`, string-list `depends_on`, and nullable string `released_in`. The status must match its containing status directory. Runtime features omit `release_track` or set it to `runtime`; completed runtime features require a release version, while unreleased runtime features use `null`. Support-tooling features set `release_track: support-tooling` and always use `released_in: null`, including after completion. Dependencies name stable feature IDs or explicit issue IDs and must match the document's direct-prerequisite section.
