# Types: gameplay validation requests

**Sources:** `runtime_gameplay_commands.gd`, runtime capture/input/condition services; schemas in `tool_catalog.py`

- Capture: exact `run_id` plus bounded output width/height/pixels; output is a fixed-path staged PNG record.
- Input: exact `run_id`, existing Input Map action, strength, pressed state, and bounded frame or millisecond hold.
- Condition: runtime scope/run ID, bounded timeout, and exactly one fixed type: play state, node existence, node count, or built-in scalar property comparison.

There is no generic expression, script, method, signal wait, regex, nested property traversal, or composable boolean grammar. All requests are identity-bound and bounded independently of the bridge client deadline.
