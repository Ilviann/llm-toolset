# Types and function libraries

References in this directory are grouped by the component that owns the contract or reusable library. Each component gets a subdirectory with its own `index.md`; that index links to the exact sections for its custom data types, wire records, collaborator protocols, and reusable function libraries.

- [`python/`](python/index.md) — Python project, discovery, error, schema, bridge-client, and editor-lifecycle contracts.
- [`python-asset-family-catalog/`](python-asset-family-catalog/index.md) — immutable family, tool, companion, composition, dispatch, and result-publication contracts.
- [`editor-bridge/`](editor-bridge/index.md) — native request, error, capability, state, discovery, and limit contracts.
- [`native-command-catalog/`](native-command-catalog/index.md) — fixed command identities, access, dispatch, retained-operation policies, handlers, capabilities, limits, and registration lifecycle.
- [`native-domain-modules/`](native-domain-modules/index.md) — built-in module interface, registrar, context, ownership, ordering, and failure contracts.
- [`asset-family-registry/`](asset-family-registry/index.md) — built-in family descriptors, typed adapter contexts, independent capabilities, semantic builders, selection, bounds, and freeze lifecycle.
- [`asset-authoring-kernel/`](asset-authoring-kernel/index.md) — exact creation/edit requests, lifecycle collaborators, results, persistence policy, and recovery errors.
- [`companion-extension-registry/`](companion-extension-registry/index.md) — public native registration, contribution handlers, descriptor metadata, capability records, bounds, and errors.
- [`asset-inspection/`](asset-inspection/index.md) — general asset-inspection request, YAML response, selector, graph, collection, inheritance, and asset-family contracts.
- [`gas-ability-inspection/`](gas-ability-inspection/index.md) — Gameplay Ability family, typed policy/tag/trigger/effect records, inheritance, bounds, capabilities, and exclusions.
- [`gas-gameplay-effect-inspection/`](gas-gameplay-effect-inspection/index.md) — Gameplay Effect family, typed configuration/component/reference records, relationships, inheritance, bounds, and exclusions.
- [`gas-supporting-asset-inspection/`](gas-supporting-asset-inspection/index.md) — Cue Notify, Attribute Set, and calculation Blueprint families, typed policies/references, bounds, and exclusions.
- [`commonui-widget-inspection/`](commonui-widget-inspection/index.md) — CommonUI root defaults, allowlisted UMG tree widgets, typed values/relationships, inheritance, bounds, capabilities, and exclusions.
- [`enhanced-input-asset-inspection/`](enhanced-input-asset-inspection/index.md) — Input Action, Mapping Context, legacy config, and trigger/modifier Blueprint records, bounds, capabilities, and exclusions.
- [`ai-asset-inspection/`](ai-asset-inspection/index.md) — Behavior Tree, Blackboard, EQS, and custom AI Blueprint records, bounds, capabilities, and exclusions.
- [`blueprint-family-policy/`](blueprint-family-policy/index.md) — supported Actor/GameMode/GameState/GameInstance/Widget classifications, operation matrix, live capabilities, result fields, and exclusions.
- [`blueprint-inspector/`](blueprint-inspector/index.md) — published-family Blueprint queries, pages, snapshots, identities, and bounded value encodings.
- [`blueprint-action-catalog/`](blueprint-action-catalog/index.md) — graph-action queries, records, opaque identities, limits, caching, and invalidation.
- [`blueprint-graph-editor/`](blueprint-graph-editor/index.md) — action-backed graph-node lifecycle, typed pin defaults, wildcard-aware connections, bounded conversions, persistent identities, and results.
- [`blueprint-block-replacement/`](blueprint-block-replacement/index.md) — complete logic-unit fingerprints, ownership boundaries, semantic replacement plans, limits, results, and recovery.
- [`blueprint-mutator/`](blueprint-mutator/index.md) — published-family Blueprint creation, compile/save, components/defaults, member variables, functions, locals, macros, custom events, diagnostics, mutation scope, and cleanup contracts.
- [`widget-tree/`](widget-tree/index.md) — Widget Blueprint tree/default records, stable widget and slot identities, structural edit requests, results, limits, and exclusions.
- [`umg-authoring/`](umg-authoring/index.md) — typed layout/style edits, property and Designer-event bindings, records, costs, limits, and exclusions.
- [`gameplay-framework-editor/`](gameplay-framework-editor/index.md) — exact default GameMode/GameInstance assignment, persistence, results, and exclusions.
- [`game-data-service/`](game-data-service/index.md) — user-defined struct schemas, Data Table rows, recursive reflected values, batching, dependencies, snapshots, and limits.
- [`level-service/`](level-service/index.md) — mounted map discovery, current-map identities, revisions, actor/component/property records, snapshots, cursors, and safe opening.
- [`level-management-service/`](level-management-service/index.md) — exact blank/template creation, bounded setup, persistence, map package closures, deletion, and recovery.
- [`level-actor-editing-service/`](level-actor-editing-service/index.md) — actor operation matrix, exact identities, transactions, scoped loading, package evidence, verification, and recovery.
- [`asset-reference-service/`](asset-reference-service/index.md) — exact reference targets, evidence categories, scan completeness, snapshots, cursors, limits, and exclusions.
- [`asset-deletion-service/`](asset-deletion-service/index.md) — exact delete requests, preflight refusals, retained outcomes, and verified persistence.
- [`windows-deployment-helper/`](windows-deployment-helper/index.md) — Windows project and Engine selection, production-companion packaging, replace-safe installation, and LM Studio/Codex settings previews.
- [`asset-family-conformance/`](asset-family-conformance/index.md) — native, Python, packaging, and cross-process family fixtures and common verification gates.
