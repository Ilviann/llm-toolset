# AI inspection contracts

## Families and selectors

The admitted `unreal-mcp-ai` schema-revision-2 companion publishes inspection-only families `behavior_tree`, `blackboard`, `environment_query`, `bt_task_blueprint`, `bt_decorator_blueprint`, `bt_service_blueprint`, `eqs_generator_blueprint`, and `eqs_context_blueprint`. Each exposes one same-named record selector. The three exact asset families compose neutral identity; the five Blueprint families compose the ordinary Blueprint result. No family publishes creation or editing.

## Behavior Tree and Blackboard records

`behavior_tree` reports the Blackboard reference, root identity, and ordered static node records. Each node carries stable identity, kind, class, support kind, authored display/comment/position when available, allowlisted settings, and relevant child, service, decorator, subtree, selector, flow-abort, inverse-condition, or observer relationships. Reused/cyclic topology and editor/runtime mismatches become bounded diagnostics.

`blackboard` reports a cycle-aware parent chain and deterministic inherited/local key records. A key retains stable identity, authored key ID, name, source asset, inheritance/local state, instance synchronization, key-type class and support kind, typed enum/class/object/base constraints, fixed persisted defaults when available, and duplicate/shadow state. `schema_snapshot` covers the complete bounded effective schema without runtime values.

## Environment Query and Blueprint records

`environment_query` preserves option order and reports each generator, ordered tests, contexts, item types, filter/scoring policy, class references, and allowlisted typed/exported parameter values. Unknown plugin generators, tests, and contexts remain explicit with `unknown_plugin_subclass` support kind.

The five custom Blueprint records identify the generated class and native AI base, declare ordinary Blueprint composition, enumerate fixed supported events or overrides with Blueprint implementation state, and report allowlisted inherited AI defaults. Task/decorator/service records include Blackboard selectors and policy defaults; generator/context records include EQS contexts or override points. Custom Blueprint variables remain owned by ordinary Blueprint inspection.

## Bounds, snapshots, and exclusions

Limits are 512 Behavior Tree nodes, depth 32, 256 Blackboard keys, parent depth 8, 64 EQS options, 256 EQS tests, 16 selectors or contexts per object, 64 diagnostics, 64 allowlisted properties, 4,096 encoded bytes per property, 65,536 value nodes, and four megabytes. Paging and graph flags on AI selectors return `invalid_argument`; structural overflow returns `response_too_large`.

Snapshots include every bounded node, key, option, test, context, policy, fixed property, reference, Blueprint override, diagnostic, and unsupported-data marker. Inspection does not execute AI, return live/debugger/PIE state, recursively inspect referenced assets, access arbitrary reflection, mutate content, compile, or save.

[Types index](index.md) · [Architecture](../../architecture/ai-asset-inspection.md) · [User guide](../../user/ai-assets.md)
