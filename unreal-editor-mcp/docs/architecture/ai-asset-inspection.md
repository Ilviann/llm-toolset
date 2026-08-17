# AI asset inspection

## Ownership and boundaries

`plugin/UnrealMCPAI/` owns the direct `AIModule`, `AIGraph`, and `BehaviorTreeEditor` dependencies. Its editor module registers eight inspection-only companion API v2 families for Behavior Trees, Blackboards, Environment Queries, and supported custom AI Blueprint bases. The base plugin retains exact asset resolution, neutral or ordinary Blueprint composition, selector routing, snapshots, bounds, non-mutation checks, transport, and safe-YAML rendering.

The companion reads persisted asset objects, generated-class defaults, and public editor graph metadata. Semantic property export uses a fixed native allowlist plus explicit Blackboard-key-selector and EQS-context collectors. Unknown plugin subclasses are retained with their class and support kind; model input cannot name properties, invoke functions, or widen traversal.

## Inspection flow

The companion registry admits `UnrealMCPAI` only when its descriptor, compiled identity, API/schema values, owning module, required modules, and exact eight-family publication agree. Exact Behavior Tree, Blackboard, and Environment Query asset classes use direct records. Blueprint-generated descendants of the five supported AI bases use overlay records and preserve ordinary Blueprint members, defaults, events, functions, and graphs.

Behavior Tree traversal projects runtime node templates into deterministic static topology, then joins matching public editor nodes by represented object to add authored position and comment. Blackboard inspection walks a cycle-aware bounded parent chain and emits inherited/local keys in deterministic schema order. EQS inspection preserves option and test order while reporting generator, context, scoring, filtering, and fixed parameters. Every bounded semantic projection contributes to the shared snapshot.

## Capability and mutation policy

Capabilities publish exactly eight read-only families and their same-named selectors only while the companion is ready. `UnrealMCPAI` registers no creation, editing, persistence, or read-back adapter. It never starts a Behavior Tree, runs a query, reads live Blackboard or debugger state, loads referenced assets for recursive inspection, compiles, saves, enables plugins, or mutates packages.

## Verification

`UnrealMCP.AI.AssetInspection` covers representative native structures, custom Blueprint overlays, deterministic fingerprints, overflow rejection, and package-dirtiness preservation. `UnrealMCP.AI.LiveFixture` persists all eight family fixtures. Python catalog and production-socket restart checks verify exact admission, root/selector composition, safe repeated reads, and restart-stable snapshots.

[Wire contracts](../types/ai-asset-inspection/index.md) · [User guide](../user/ai-assets.md) · [Architecture index](index.md)
