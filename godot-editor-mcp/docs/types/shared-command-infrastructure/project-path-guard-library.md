# Library: project-path guard

**Source:** `plugin/addons/godot_mcp/project_path_guard.gd`

Validates model-facing project-relative or `res://` paths, allowed extensions, existence policy, protected write destinations, traversal, and symbolic-link confinement. Consumers inject this guard rather than resolve an unvalidated model path through Godot filesystem/resource APIs.
