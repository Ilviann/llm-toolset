"""Data-driven released capability, family, operation, and limit contract."""

from __future__ import annotations

from unreal_editor_mcp import __version__


def verify_capability_contract(capabilities: dict[str, object], state: dict[str, object]) -> None:
    if capabilities.get("commands") != [
        "capabilities", "editor_state", "editor_shutdown", "operation_status", "operation_cancel",
        "asset_inspect", "asset_references", "asset_delete",
        "level_inspect", "level_open", "level_manage", "level_actor_edit", "level_save",
        "blueprint_action_catalog", "blueprint_graph_edit",
        "blueprint_block_replace",
        "blueprint_create", "blueprint_compile", "blueprint_save",
        "blueprint_component_edit", "blueprint_default_edit", "blueprint_member_edit",
        "widget_tree_edit", "gameplay_framework_edit", "game_data_inspect", "game_data_edit",
    ]:
        raise AssertionError("released command catalog mismatch")
    if capabilities.get("bridge_version") != __version__ or state.get("bridge_ready") is not True:
        raise AssertionError("capability/state contract mismatch")
    if capabilities.get("features", {}).get("graceful_editor_shutdown") is not True:
        raise AssertionError("graceful editor shutdown capability is unavailable")
    if capabilities.get("features", {}).get("asset_inspection_core") is not True:
        raise AssertionError("asset inspection core capability is unavailable")
    if capabilities.get("features", {}).get("asset_inspect_data") is not True:
        raise AssertionError("asset-inspect-data capability is unavailable")
    expected_asset_inspect_limits = {
        "asset_inspect_page_size": 100,
        "asset_inspect_selector_bytes": 1024,
        "asset_inspect_complete_graph_bytes": 65536,
    }
    if any(capabilities.get("limits", {}).get(name) != value
           for name, value in expected_asset_inspect_limits.items()):
        raise AssertionError(f"asset inspection limits mismatch: {capabilities.get('limits')!r}")
    if capabilities.get("features", {}).get("blueprint_mutation") is not True:
        raise AssertionError("Phase 6 mutation capability is unavailable")
    for feature in ("blueprint_functions", "blueprint_local_variables", "blueprint_rep_notify"):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"Phase 6 capability is unavailable: {feature}")
    for feature in (
        "blueprint_function_replacement",
        "blueprint_function_replacement_scratch_preflight",
        "blueprint_macro_replacement",
        "blueprint_custom_event_replacement",
        "blueprint_event_replacement",
        "blueprint_logic_unit_external_connections",
        "blueprint_node_layout",
    ):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"function replacement capability is unavailable: {feature}")
    for feature in ("blueprint_macros", "blueprint_custom_events"):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"Phase 7 capability is unavailable: {feature}")
    if capabilities.get("features", {}).get("blueprint_action_catalog") is not True:
        raise AssertionError("Phase 10 action catalog capability is unavailable")
    for feature in ("blueprint_graph_mutation", "blueprint_graph_node_lifecycle"):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"Phase 11 graph capability is unavailable: {feature}")
    for feature in ("blueprint_graph_pin_defaults", "blueprint_graph_direct_connections"):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"Phase 12 graph capability is unavailable: {feature}")
    for feature in ("blueprint_graph_wildcard_specialization", "blueprint_graph_automatic_conversion"):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"Phase 13 graph capability is unavailable: {feature}")
    for feature in ("blueprint_family_policy", "game_mode_families", "game_state_families"):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"Phase 14 family capability is unavailable: {feature}")
    if capabilities.get("features", {}).get("game_instance_family") is not True:
        raise AssertionError("Phase 15 GameInstance capability is unavailable")
    if capabilities.get("features", {}).get("widget_blueprint_family") is not True \
            or capabilities.get("features", {}).get("widget_tree_authoring") is not True:
        raise AssertionError("Widget Blueprint capability is unavailable")
    for feature in (
        "umg_layout_authoring", "umg_style_authoring",
        "umg_property_bindings", "umg_designer_events",
    ):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"UMG authoring capability is unavailable: {feature}")
    for feature in ("multiplayer_blueprint_authoring", "custom_event_rpcs",
                    "typed_replication_settings", "gameplay_framework_assignment"):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"Phase 16 multiplayer capability is unavailable: {feature}")
    for feature in ("user_defined_struct_authoring", "typed_data_tables", "game_data_batch_editing"):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"Phase 17 game-data capability is unavailable: {feature}")
    for feature in ("level_discovery", "level_open", "level_snapshots"):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"level-open capability is unavailable: {feature}")
    for feature in (
        "level_actor_inspection",
        "level_world_partition_descriptors",
        "level_targeted_actor_loading",
        "level_instance_properties",
    ):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"level-inspect capability is unavailable: {feature}")
    for feature in (
        "level_management", "level_blank_creation", "level_template_creation",
        "level_world_settings", "level_map_deletion",
    ):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"level-management capability is unavailable: {feature}")
    for feature in (
        "level_actor_editing", "level_actor_transactions",
        "level_package_save_verification",
    ):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"level-edit capability is unavailable: {feature}")
    if capabilities.get("features", {}).get("level_world_partition_conversion") is not False:
        raise AssertionError("level World Partition conversion must remain unsupported")
    for feature in ("asset_reference_discovery", "asset_reference_live_memory"):
        if capabilities.get("features", {}).get(feature) is not True:
            raise AssertionError(f"asset-references capability is unavailable: {feature}")
    if capabilities.get("features", {}).get("asset_delete") is not True \
            or capabilities.get("features", {}).get("asset_delete_force") is not False \
            or capabilities.get("features", {}).get("asset_delete_undo") is not False:
        raise AssertionError("asset-delete capability contract mismatch")
    family_matrix = capabilities.get("blueprint_families", [])
    base_families = [
        "actor", "game_mode_base", "game_mode", "game_state_base", "game_state", "game_instance",
        "widget",
    ]
    family_names = [record.get("family") for record in family_matrix]
    if family_names[:len(base_families)] != base_families \
            or len(family_names) != len(set(family_names)):
        raise AssertionError(f"Phase 15 family matrix mismatch: {family_matrix!r}")
    for record in family_matrix[:len(base_families)]:
        operations = record.get("operations", {})
        assignable = record.get("family") in {"game_mode_base", "game_mode", "game_instance"}
        if operations.get("graph_edit") is not True or operations.get("parent_change") is not False \
                or operations.get("project_settings_assignment") is not assignable:
            raise AssertionError(f"Phase 16 operation matrix mismatch: {record!r}")
        if not isinstance(record.get("multiplayer", {}).get("rpc_modes"), list):
            raise AssertionError(f"Phase 16 multiplayer matrix mismatch: {record!r}")
        family = record.get("family")
        if operations.get("components") != (family not in {"game_instance", "widget"}) \
                or operations.get("widget_tree") != (family == "widget"):
            raise AssertionError(f"Blueprint family operation matrix mismatch: {record!r}")
    for record in family_matrix[len(base_families):]:
        operations = record.get("operations", {})
        if not isinstance(record.get("extension_id"), str) \
                or operations.get("discover") is not True \
                or operations.get("inspect") is not True \
                or any(operations.get(name) is not False for name in (
                    "create", "compile", "save", "class_defaults", "components", "widget_tree",
                    "member_variables", "functions", "local_variables", "macros", "custom_events",
                    "action_catalog", "graph_edit", "parent_change", "project_settings_assignment",
                )):
            raise AssertionError(f"Companion family operation matrix mismatch: {record!r}")
        if not isinstance(record.get("multiplayer", {}).get("rpc_modes"), list):
            raise AssertionError(f"Companion family multiplayer matrix mismatch: {record!r}")
    expected_graph_limits = {
        "graph_nodes": 2048, "graph_pins_per_node": 256, "graph_coordinate": 1000000,
        "graph_links_per_pin": 64, "graph_automatic_conversion_nodes": 1, "pin_default_chars": 512,
    }
    if any(capabilities.get("limits", {}).get(name) != value for name, value in expected_graph_limits.items()):
        raise AssertionError(f"Phase 13 graph limits mismatch: {capabilities.get('limits')!r}")
    expected_replacement_limits = {
        "function_replacement_nodes": 64,
        "function_replacement_owned_nodes": 256,
        "function_replacement_locals": 64,
        "function_replacement_defaults": 128,
        "function_replacement_connections": 256,
        "logic_unit_replacement_nodes": 64,
        "logic_unit_replacement_owned_nodes": 256,
        "logic_unit_replacement_locals": 64,
        "logic_unit_replacement_defaults": 128,
        "logic_unit_replacement_connections": 256,
        "logic_unit_external_connections": 64,
        "logic_unit_layout_nodes": 322,
        "logic_unit_layout_edges": 1024,
        "logic_unit_layout_iterations": 8,
        "logic_unit_layout_collision_probes": 128,
        "logic_unit_layout_work": 2000000,
        "logic_unit_layout_ms": 100,
    }
    if any(capabilities.get("limits", {}).get(name) != value
           for name, value in expected_replacement_limits.items()):
        raise AssertionError(
            f"function replacement limits mismatch: {capabilities.get('limits')!r}")
    expected_game_data_limits = {
        "game_data_fields": 64, "game_data_rows": 2048, "game_data_batch_rows": 64,
        "game_data_collection_items": 64, "game_data_depth": 4, "game_data_dependencies": 256,
    }
    if any(capabilities.get("limits", {}).get(name) != value for name, value in expected_game_data_limits.items()):
        raise AssertionError(f"Phase 17 game-data limits mismatch: {capabilities.get('limits')!r}")
    expected_widget_limits = {
        "widget_tree_widgets": 512,
        "widget_tree_depth": 32,
        "widget_named_slots": 256,
        "widget_defaults_per_widget": 16,
        "widget_changed_defaults": 1024,
        "widget_bindings": 256,
    }
    if any(capabilities.get("limits", {}).get(name) != value
           for name, value in expected_widget_limits.items()):
        raise AssertionError(f"Widget-tree limits mismatch: {capabilities.get('limits')!r}")
    if capabilities.get("limits", {}).get("level_discovery_scan") != 2048 \
            or capabilities.get("limits", {}).get("level_external_packages") != 2048:
        raise AssertionError(f"level-open limits mismatch: {capabilities.get('limits')!r}")
    if capabilities.get("limits", {}).get("level_setup_properties") != 16 \
            or capabilities.get("limits", {}).get("level_owned_packages") != 2048:
        raise AssertionError(f"level-management limits mismatch: {capabilities.get('limits')!r}")
    expected_level_inspect_limits = {
        "level_actor_scan": 4096,
        "level_actor_records": 2048,
        "level_components": 64,
        "level_actor_tags": 64,
        "level_data_layers": 32,
        "level_targeted_loads": 1,
    }
    if any(capabilities.get("limits", {}).get(name) != value
           for name, value in expected_level_inspect_limits.items()):
        raise AssertionError(
            f"level-inspect limits mismatch: {capabilities.get('limits')!r}")
    expected_level_edit_limits = {
        "level_edit_operations": 32,
        "level_edit_actors": 64,
        "level_save_packages": 64,
    }
    if any(capabilities.get("limits", {}).get(name) != value
           for name, value in expected_level_edit_limits.items()):
        raise AssertionError(
            f"level-edit limits mismatch: {capabilities.get('limits')!r}")
    expected_asset_reference_limits = {
        "asset_reference_registry_candidates": 4096,
        "asset_reference_live_objects": 8192,
        "asset_reference_records": 2048,
        "asset_reference_assets_per_package": 64,
        "asset_reference_properties": 16,
        "asset_reference_retained_cursors": 8,
        "asset_reference_traversal_depth": 1,
    }
    if any(capabilities.get("limits", {}).get(name) != value
           for name, value in expected_asset_reference_limits.items()):
        raise AssertionError(
            f"asset-references limits mismatch: {capabilities.get('limits')!r}"
        )
    if capabilities.get("asset_access") != {
        "read_scope": "all_mounted_content",
        "mutation_scope": "project_content_and_local_project_plugins",
    }:
        raise AssertionError("asset access policy contract mismatch")
