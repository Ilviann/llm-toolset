# Shared Godot command infrastructure

## Purpose

Provide the low-level guards, codecs, bounded records, stable errors, limits, cursors, operation identities, and event identities used across editor command services.

## Owned source

- Guards/codecs: `project_path_guard.gd`, `scene_node_access.gd`, `property_value_codec.gd`, `input_event_codec.gd`.
- Records/identity: `project_identity.gd`, `atomic_json_record.gd`, `cursor_store.gd`, `operation_registry.gd`, `event_store.gd`.
- Policy/envelopes: `command_limits.gd`, `error_envelope.gd`.

## Dependencies

These scripts depend only on Godot APIs and other files in this component where explicitly noted. Higher editor components receive them through composition. Mirrored limits, errors, project identities, and value forms are contract-coupled to Python.

## Invariants

- Model-facing paths reject traversal, symlink escape, invalid forms, and protected destinations.
- Scene node access remains relative to the selected scene root.
- Property values are recursively bounded, finite, explicitly tagged for non-JSON types/references, and validated against Godot property hints.
- Input events normalize only the supported fixed key/mouse/joypad vocabulary.
- Error envelopes have stable codes and bounded public details.
- Cursor IDs are opaque, bounded, expiring, query-bound, and snapshot-bound.
- Project identities normalize Windows/POSIX paths consistently across languages.
- Atomic JSON records are bounded and replaced through same-directory temporary files.
- Operation/event registries have monotonic/process-scoped identities and bounded retention.

## Change and verification guide

Changes here have high fan-out. Search every consumer, update capabilities and Python mirrors, and expand only the affected documentation/type folders. Run Python contract/schema tests and the Phase 5, 7, and 11 headless tests at minimum; add state/runtime/service tests for the consumers touched.
