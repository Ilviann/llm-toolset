import unittest

from unreal_editor_mcp.errors import DomainError, ErrorCode, bridge_error_from_payload


class ErrorTests(unittest.TestCase):
    def test_error_envelope_is_bounded(self):
        error = DomainError("x" * 1000, details={f"key{i}": "y" * 1000 for i in range(30)})
        payload = error.as_dict()
        self.assertLessEqual(len(payload["message"]), 512)
        self.assertLessEqual(len(payload["details"]), 16)
        self.assertTrue(all(len(value) <= 512 for value in payload["details"].values()))

    def test_unknown_bridge_error_code_is_sanitized(self):
        error = bridge_error_from_payload({"code": "invented", "message": "failure", "details": []})
        self.assertEqual(error.code, ErrorCode.INTERNAL_ERROR)
        self.assertEqual(error.details, {})
