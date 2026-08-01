# Workspace feature documentation

## Status groups

- Planned: None.
- Active: None.
- Completed: None.
- [Deferred features](deferred/index.md).

## Front matter contract

Every feature document begins with YAML front matter containing string `feature_id`, enum `status`, string-list `depends_on`, and nullable string `released_in`. The status must match its containing status directory. Completed features require a release version; unreleased features use `null`. Dependencies name stable feature IDs or explicit issue IDs and must match the document's direct-prerequisite section.
