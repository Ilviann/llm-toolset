# Phase 19 — UMG layout, styling, bindings, and UI logic

**Outcome:** Agents can turn a Widget Blueprint hierarchy into a practical responsive HUD or menu by configuring layout and appearance and wiring supported widget events and data flow.

### Implementation

- Extend `widget_tree_edit` with typed live slot operations for the common panel families, including anchors, offsets, alignment, sizing, padding, row/column placement, spans, ordering, and z-order where the exact slot class supports them.
- Add bounded typed widget styling and presentation values required by common HUD/menu widgets, including visibility, enabled/focus behavior, text, colors, brushes, fonts, progress values, and compatible asset references. Reuse the Phase 17 recursive reflected-value codec without exposing unrestricted reflection or serialized Slate structures.
- Add exact inspection and mutation of supported widget delegate and property bindings. Prefer event-driven updates, validate source/target signatures and widget identities live, report binding cost/capability, and preserve unrelated bindings.
- Extend action discovery and graph editing for Widget Blueprint-specific events, widget references, construction/destruction/focus/input flow, and supported designer-created delegate handlers. Keep every action context-valid and opaque; do not synthesize arbitrary calls or graphs.
- Preserve Designer hierarchy, layout, styles, graph logic, bindings, animations, and unrelated Blueprint members outside the changed boundary. Continue using exact snapshots, operation reconciliation, bounded diagnostics, compile/save, and explicit restoration.
- Do not add Widget animations, screenshot-driven design, arbitrary Slate code, runtime viewport control, input injection, or gameplay object mutation.

### Verification

- Author representative responsive health/ammo HUD, damage indicator container, scoreboard, pause menu, and settings menu using common Canvas, Overlay, Box, Grid, Scroll/List, Text, Image, Button, and ProgressBar behavior.
- Wire representative button, selection, focus, and value-update flows; verify exact widget references, compatible event signatures, binding inspection, compilation, save/reload, and graph read-back.
- Test incompatible slots and bindings, unsafe/unsupported style fields, missing assets, responsive anchor/offset edge cases, bounds, stale snapshots, rollback, undo/redo, timeout, replay, and lost-response recovery.
- Prove untouched Designer, graph, binding, and animation fingerprints remain stable across accepted and rejected operations.
- Run the complete practical UMG authoring suite natively on macOS and Windows.

### Documentation and completion gate

- Document responsive layout, supported slot/style values, event-driven binding guidance, UI graph workflows, performance implications, exclusions, and complete HUD/menu examples.
- Complete the phase only when agents can produce and update representative functional HUD/menu Widget Blueprints while preserving unrelated content on both native platforms.

[Back to roadmap](../../ROADMAP.md) · [Shared roadmap contracts](index.md)
