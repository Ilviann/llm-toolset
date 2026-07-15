from __future__ import annotations

import unittest

from godot_editor_mcp.errors import InvalidResponseError
from godot_editor_mcp.state_payloads import EditorStatePayload, ReloadStatusPayload


class EditorStatePayloadTests(unittest.TestCase):
    def test_view_validates_only_fields_used_by_a_predicate(self) -> None:
        state = EditorStatePayload.from_payload({
            "scene": "res://main.tscn",
            "active_operations": [{"operation_id": "op-1"}],
        })
        self.assertEqual(state.scene, "res://main.tscn")
        self.assertTrue(state.operation_active("op-1"))
        with self.assertRaisesRegex(InvalidResponseError, "playing"):
            _ = state.playing

    def test_malformed_operation_and_import_identities_are_rejected(self) -> None:
        with self.assertRaisesRegex(InvalidResponseError, "operation identity"):
            EditorStatePayload.from_payload({
                "active_operations": [{"kind": "open_scene"}],
            }).operation_active("op-1")
        with self.assertRaisesRegex(InvalidResponseError, "operation_id"):
            _ = EditorStatePayload.from_payload({
                "recent_imports": [{
                    "path": "res://bad.png", "status": "failed",
                }],
            }).recent_imports


class ReloadStatusPayloadTests(unittest.TestCase):
    def test_complete_identity_payload_is_preserved(self) -> None:
        payload = {
            "completed": True,
            "status": "completed",
            "operation_id": "op-1",
            "project_hash": "a" * 64,
            "bridge_version": "0.10.0",
            "recovered": True,
        }
        view = ReloadStatusPayload.from_payload(payload)
        self.assertEqual(view.as_dict(), payload)

    def test_missing_or_inconsistent_identity_fields_are_rejected(self) -> None:
        base = {
            "completed": False,
            "status": "pending",
            "operation_id": "op-1",
            "project_hash": "a" * 64,
            "bridge_version": "0.10.0",
        }
        for change in (
            {"operation_id": None},
            {"project_hash": "short"},
            {"bridge_version": ""},
            {"completed": True},
        ):
            with self.subTest(change=change):
                with self.assertRaises(InvalidResponseError):
                    ReloadStatusPayload.from_payload({**base, **change})


if __name__ == "__main__":
    unittest.main()
