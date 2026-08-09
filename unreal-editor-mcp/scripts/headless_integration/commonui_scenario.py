"""CommonUI companion inspection scenario."""

from __future__ import annotations

import uuid

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.errors import BridgeError, ErrorCode


def verify_commonui_inspection(bridge: UnrealBridge) -> None:
    commonui_path = "/Game/UnrealMCPCommonUI/WBP_InspectionFixture.WBP_InspectionFixture"
    commonui_inspection = bridge.call("blueprint_inspect", {
        "mode": "inspect",
        "asset_path": commonui_path,
        "sections": [
            "summary", "parent_class", "widget_tree", "commonui_widget",
            "commonui_activation", "commonui_references",
        ],
        "include_inherited": True,
        "page_size": 100,
    })
    commonui_snapshot = commonui_inspection.get("snapshot_id")
    commonui_records = {
        record.get("section"): record
        for record in commonui_inspection.get("records", [])
        if isinstance(record, dict)
    }
    expected_commonui_sections = {
        "summary", "parent_class", "commonui_widget",
        "commonui_activation", "commonui_references",
    }
    activation = commonui_records.get("commonui_activation", {})
    references = commonui_records.get("commonui_references", {})
    if not isinstance(commonui_snapshot, str) or len(commonui_snapshot) != 40 \
            or commonui_inspection.get("blueprint_family") != "widget" \
            or not expected_commonui_sections.issubset(commonui_records) \
            or activation.get("auto_activate", {}).get("value") is not True \
            or activation.get("is_back_handler", {}).get("value") is not True \
            or references.get("action_domain_override", {}).get("resolved") is not False \
            or references.get("action_domain_override", {}).get("object_path") \
            != "/Game/UnrealMCPCommonUI/DA_UnresolvedDomain.DA_UnresolvedDomain":
        raise AssertionError(f"CommonUI inspection is incomplete: {commonui_inspection!r}")
    repeated_commonui = bridge.call("blueprint_inspect", {
        "mode": "inspect", "asset_path": commonui_path,
        "sections": [
            "summary", "parent_class", "widget_tree", "commonui_widget",
            "commonui_activation", "commonui_references",
        ],
        "include_inherited": True,
        "page_size": 100,
    })
    typed_commonui_records = [
        record for record in commonui_inspection.get("records", [])
        if isinstance(record, dict) and str(record.get("section", "")).startswith("commonui_")
    ]
    repeated_typed_commonui_records = [
        record for record in repeated_commonui.get("records", [])
        if isinstance(record, dict) and str(record.get("section", "")).startswith("commonui_")
    ]
    if repeated_commonui.get("snapshot_id") != commonui_snapshot \
            or repeated_typed_commonui_records != typed_commonui_records:
        raise AssertionError("CommonUI inspection is not deterministic")

