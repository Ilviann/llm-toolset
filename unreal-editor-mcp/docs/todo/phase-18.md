# Phase 18 — Widget Blueprint family and widget trees

**Outcome:** Agents can create and safely modify the structural widget hierarchy of HUD and menu Widget Blueprints while retaining the established Blueprint compile, save, inspection, and graph contracts.

### Implementation

- Add `UUserWidget`-derived Widget Blueprints to the explicit family policy as a non-Actor family with an exact capability matrix. Reuse compatible defaults, variables, functions, locals, macros, custom events, action cataloging, graph editing, compilation, saving, diagnostics, snapshots, and operation reconciliation; reject Actor component operations explicitly.
- Extend `blueprint_create` and `blueprint_inspect` for Widget Blueprint creation and bounded inspection of summary, parent, compile state, Blueprint members/graphs, widget tree, widget defaults, panel slots, ownership, and stable identities.
- Add `widget_tree_edit` for one typed add, remove, rename, reparent, root, variable-exposure, or safe widget-default mutation. Resolve only live usable `UWidget` classes and preserve required roots, named slots, panel-child rules, ownership, and unrelated widget content.
- Make hierarchy and slot identities explicit and require an authoritative Widget Blueprint snapshot for mutation. Bound tree depth, widget count, named-slot traversal, changed properties, references, diagnostics, and Game-thread work.
- Support the common structural building blocks needed by HUDs and menus, including panels, text, images, buttons, progress bars, lists, and user-widget composition when their live classes and slot rules permit it. Exact class availability remains authoritative.
- Keep detailed responsive layout, styles, delegate/property bindings, and Widget Blueprint-specific event wiring for Phase 19. Keep Widget animations and arbitrary Slate/C++ widget authoring outside scope.

### Verification

- Create representative HUD root, status panel, crosshair, scoreboard/list, pause menu, settings panel, and reusable user-widget hierarchies.
- Test root and panel rules, named slots, nested user widgets, stable identities, variable exposure, invalid classes, cycles, incompatible reparenting, destructive reference checks, tree/response limits, and unsupported Actor component operations.
- Test compile diagnostics, stale snapshots, transaction restoration, undo/redo, save/reload, timeout, replay, lost-response recovery, and unchanged-content fingerprints.
- Run the complete Widget Blueprint family and tree suite natively on macOS and Windows.

### Documentation and completion gate

- Document Widget Blueprint capabilities, structural composition, supported tree operations, identity and reference rules, limits, and focused HUD/menu hierarchy examples.
- Complete the phase only when representative widget trees can be created, modified, compiled, saved, restarted, and read back without altering unrelated Blueprint or Designer content on both native platforms.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
