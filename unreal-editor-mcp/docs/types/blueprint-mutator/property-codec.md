# Reflected default-property codec

The shared property policy is used by targeted inspection, component-template writes, Blueprint class-default writes, result read-back, and structural fingerprints. A property must be a single editable template value and must not be transient, deprecated, template-disabled, a delegate/interface, or an array/set/map.

Supported JSON forms are:

- Boolean properties: JSON Boolean.
- Finite numeric properties: JSON number; integer values must be integral and within exact JSON integer range.
- name, string, and text: JSON string.
- enum: enumerator-name string; flags enums: bounded array of enumerator-name strings.
- Vector/Vector2D/Vector4, Rotator, Quat, Transform, Color/LinearColor, IntPoint/IntVector/IntVector4: bounded canonical Unreal import-text string.
- hard/soft class references: exact compatible class path string or an empty string for null.
- hard/soft object references: exact compatible visible packageable asset path string or an empty string for null.

References must resolve, satisfy the reflected property class, and not be transient or editor-only. Hard arbitrary UObject graphs, Actor/component instances, raw pointers, delegates, unsupported structs, and containers reject. Inspection returns `supported: false` for a named property outside this policy rather than recursively reflecting it.
