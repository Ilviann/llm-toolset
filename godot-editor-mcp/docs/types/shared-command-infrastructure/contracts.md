# Shared command infrastructure contracts

Use the index to retrieve only the contract section relevant to the task.

## Type: error envelope

**Source:** `plugin/addons/godot_mcp/error_envelope.gd`

Editor handlers return a common dictionary boundary. Success contains `ok: true` and a result. Failure contains `ok: false` plus stable `code`, public `message`, bounded JSON-safe `details`, and `retryable`.

Codes mirror Python `ErrorCode`; changing them requires contract tests and typed Python decoding updates. Construction and compatibility helpers are documented separately in [Library: error envelopes](#library-error-envelopes).

## Library: error envelopes

**Source:** `plugin/addons/godot_mcp/error_envelope.gd`

Centralizes mirrored error-code constants, constructs success/failure dictionaries, bounds untrusted detail depth/size, classifies legacy string failures, and extracts safe public messages. All editor command services use these helpers instead of inventing response shapes.

## Library: command limits

**Source:** `plugin/addons/godot_mcp/command_limits.gd`

Constants bound bridge bytes/clients/deadlines, traversals, result pages, cursor count/lifetime/length, diagnostics, settings/input/autoload batches, scene construction/transactions, value nesting/elements/strings/packed arrays, runtime requests/captures/conditions, and retained state.

Handlers import this library rather than repeating numbers. Public effective limits are reported through capabilities and mirrored by Python schemas/transport checks. A limit change is incomplete until both languages, capabilities, README where user-relevant, and contract/boundary tests agree.

## Library: project identity

**Source:** `plugin/addons/godot_mcp/project_identity.gd`

Normalizes the configured project path with explicit Windows and POSIX rules and hashes the normalized string with SHA-256. Discovery and reload records use this stable identity instead of exposing an absolute path.

The algorithm mirrors `godot_editor_mcp.discovery`. Path separator, drive-letter/case, resolution, encoding, or hash changes are protocol migrations and must be tested in both languages for both platform branches.

## Library: atomic JSON records

**Source:** `plugin/addons/godot_mcp/atomic_json_record.gd`

Reads a bounded JSON object and atomically replaces it using a same-directory temporary file. Writes serialize compact JSON, flush/close, rename into place, and clean the temporary path on failure. Reads reject missing/oversized/malformed/non-object data with controlled results.

Discovery and reload are the current consumers. Their record schemas remain owned by those components; this library owns only bounded durable file mechanics. Do not use prose files or arbitrary model paths as record input.

## Type: cursor record

**Source:** `plugin/addons/godot_mcp/cursor_store.gd`

Private bounded record keyed by a random opaque 48-character cursor ID. It stores a read kind, normalized-query fingerprint, snapshot ID, next offset, and creation/expiry information.

At most 128 two-minute records are retained; IDs contain no token or Godot object reference. Store operations are documented separately in [Library: cursor store](#library-cursor-store).

## Library: cursor store

**Source:** `plugin/addons/godot_mcp/cursor_store.gd`

`issue` creates an opaque record; `prepare` validates syntax/kind/query before a remote snapshot is known; `resume` additionally validates the snapshot and returns the next offset. Expiry pruning and oldest-record eviction enforce bounds. Errors distinguish malformed/expired/query-mismatched cursors from stale snapshots.

## Type: operation record

**Source:** `plugin/addons/godot_mcp/operation_registry.gd`

Process-scoped record for an accepted asynchronous editor action. It carries an opaque operation ID, kind, accepted/completed state, optional run identity, concise details, and bounded recency information.

Trackers/services bind these records to observed transitions. Registry operations are documented separately in [Library: operation registry](#library-operation-registry); do not infer completion from state alone when an operation ID exists.

## Library: operation registry

**Source:** `plugin/addons/godot_mcp/operation_registry.gd`

Allocates accepted operation IDs, completes exact IDs or kinds, restores a completed reload after restart, and returns bounded concise active/recent views. Services accept before starting work and complete only when their owning tracker observes the intended transition.

## Type: event record

**Source:** `plugin/addons/godot_mcp/event_store.gd`

Bounded timestamped editor event with a monotonically increasing event ID, kind, and concise details. Scene, run, import/filesystem, and related trackers use the shared sequence so clients can correlate state changes without separate clocks.

Event IDs are observation cursors, not operation IDs and not durable across editor processes. Store operations are documented separately in [Library: event store](#library-event-store).

## Library: event store

**Source:** `plugin/addons/godot_mcp/event_store.gd`

Allocates the shared monotonic event sequence, timestamps concise event dictionaries, retains bounded recent history, and exposes the latest identity used by aggregate state. Trackers use this library so event ordering is comparable across state families.

## Type: property value contract

**Source:** `plugin/addons/godot_mcp/property_value_codec.gd`

Model-facing representation of bounded Godot Variant values. Ordinary JSON scalars, arrays, and dictionaries remain direct. Non-JSON or reference values use explicit `$type` dictionaries for node/resource/NodePath, rectangles/transforms/quaternion/plane/AABB/basis, enum/flags, and packed arrays. Concise numeric arrays remain supported for common vectors/colors.

Validation and conversion operations are documented separately in [Library: property value codec](#library-property-value-codec).

## Library: property value codec

**Source:** `plugin/addons/godot_mcp/property_value_codec.gd`

`convert` decodes the model-facing property value type for a target Godot property; `encode` converts a Godot Variant into bounded JSON/tagged output; `supported_forms` feeds capabilities. Helpers validate finite numbers, depth, elements, keys, strings, packed arrays, encoded bytes, Variant/property hints, resource/node classes, and scene confinement. Transaction-local node handles are available only through an injected resolver.

## Type: input event contract

**Source:** `plugin/addons/godot_mcp/input_event_codec.gd`

Normalized dictionaries represent the supported Input Map events:

- key: logical/numeric key, optional physical mode and modifiers;
- mouse button: fixed name/number and device;
- joypad button: fixed name/number and device;
- joypad motion: fixed axis, direction `-1|1`, and device.

Decoding and normalization operations are documented separately in [Library: input event codec](#library-input-event-codec).

## Library: input event codec

**Source:** `plugin/addons/godot_mcp/input_event_codec.gd`

Decodes bounded model values into supported Godot `InputEvent` objects and normalizes existing events for exact comparison/responses. Unknown event kinds, keys, axes/buttons, modifiers, devices, or out-of-range values fail. Transactional Input Map mutation remains in its owning project-workflow component.

## Library: project-path guard

**Source:** `plugin/addons/godot_mcp/project_path_guard.gd`

Validates model-facing project-relative or `res://` paths, allowed extensions, existence policy, protected write destinations, traversal, and symbolic-link confinement. Consumers inject this guard rather than resolve an unvalidated model path through Godot filesystem/resource APIs.

## Library: scene-node access

**Source:** `plugin/addons/godot_mcp/scene_node_access.gd`

Validates scene-relative node paths and node names, resolves `.` or descendants only inside the edited scene root, and returns shared success/failure envelopes. Scene node paths are a distinct vocabulary from project filesystem paths.
