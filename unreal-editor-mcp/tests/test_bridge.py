import json
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from unreal_editor_mcp.bridge import MAX_RESPONSE_BYTES, UnrealBridge
from unreal_editor_mcp.discovery import DiscoveryRecord
from unreal_editor_mcp.errors import BridgeError, ErrorCode
from unreal_editor_mcp.project import ProjectLayout


RECORD = DiscoveryRecord("a" * 40, 123, 15485, "0.4.0", "5.8.0", 1)


class FakeResponse:
    def __init__(self, status=200, body=b'{"ok":true,"result":{"project_hash":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}}'):
        self.status = status
        self.body = body

    def read(self, amount):
        return self.body[:amount]


class FakeConnection:
    instances = []

    def __init__(self, host, port, timeout):
        self.host, self.port, self.timeout = host, port, timeout
        self.request_data = None
        self.response = FakeResponse()
        self.closed = False
        self.__class__.instances.append(self)

    def request(self, method, path, body, headers):
        self.request_data = method, path, body, headers

    def getresponse(self):
        return self.response

    def close(self):
        self.closed = True


class BridgeTests(unittest.TestCase):
    def setUp(self):
        FakeConnection.instances.clear()
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        descriptor = root / "Example.uproject"
        descriptor.write_text("{}", encoding="utf-8")
        self.layout = ProjectLayout.resolve(descriptor)

    def _bridge(self, factory=FakeConnection, **kwargs):
        return UnrealBridge(self.layout, connection_factory=factory, **kwargs)

    @patch("unreal_editor_mcp.bridge.read_token", return_value="b" * 64)
    @patch("unreal_editor_mcp.bridge.read_discovery", return_value=RECORD)
    def test_authenticated_loopback_request(self, _discovery, _token):
        result = self._bridge().call("capabilities")
        connection = FakeConnection.instances[-1]
        self.assertEqual((connection.host, connection.port), ("127.0.0.1", 15485))
        self.assertEqual(connection.request_data[3]["Authorization"], "Bearer " + "b" * 64)
        self.assertEqual(result["project_hash"], "a" * 40)
        self.assertTrue(connection.closed)

    @patch("unreal_editor_mcp.bridge.read_token", return_value="b" * 64)
    @patch("unreal_editor_mcp.bridge.read_discovery", return_value=RECORD)
    def test_sends_bounded_inspection_arguments(self, _discovery, _token):
        arguments = {"mode": "discover", "package_path": "/Game", "page_size": 5}
        self._bridge().call("blueprint_inspect", arguments)
        body = json.loads(FakeConnection.instances[-1].request_data[2])
        self.assertEqual(body, {"command": "blueprint_inspect", "arguments": arguments})

    @patch("unreal_editor_mcp.bridge.read_token", return_value="b" * 64)
    @patch("unreal_editor_mcp.bridge.read_discovery", return_value=RECORD)
    def test_sends_phase_four_mutation_arguments(self, _discovery, _token):
        operation_id = "c" * 32
        snapshot = "d" * 40
        calls = (
            ("blueprint_create", {"operation_id": operation_id, "parent_class": "/Script/Engine.Actor", "package_path": "/Game/BP_New"}),
            ("blueprint_compile", {"operation_id": operation_id, "asset_path": "/Game/BP_New.BP_New", "expected_snapshot": snapshot}),
            ("blueprint_save", {"operation_id": operation_id, "asset_path": "/Game/BP_New.BP_New", "expected_snapshot": snapshot}),
            ("blueprint_component_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_New.BP_New", "expected_snapshot": snapshot,
                "operation": "add", "component_class": "/Script/Engine.SceneComponent", "name": "Root"}),
            ("blueprint_default_edit", {"operation_id": operation_id, "asset_path": "/Game/BP_New.BP_New", "expected_snapshot": snapshot,
                "property_name": "InitialLifeSpan", "value": 1.0}),
        )
        bridge = self._bridge()
        for command, arguments in calls:
            bridge.call(command, arguments)
            body = json.loads(FakeConnection.instances[-1].request_data[2])
            self.assertEqual(body, {"command": command, "arguments": arguments})

    @patch("unreal_editor_mcp.bridge.read_token", return_value="b" * 64)
    @patch("unreal_editor_mcp.bridge.read_discovery", return_value=DiscoveryRecord("a" * 40, 1, 15485, "9.0.0", "5.8", 1))
    def test_version_mismatch_allows_capabilities_but_rejects_state(self, _discovery, _token):
        self._bridge().call("capabilities")
        with self.assertRaises(BridgeError) as caught:
            self._bridge().call("editor_state")
        self.assertEqual(caught.exception.code, ErrorCode.VERSION_MISMATCH)

    @patch("unreal_editor_mcp.bridge.read_discovery", return_value=RECORD)
    def test_port_mismatch_rejected_before_token_read(self, _discovery):
        with self.assertRaises(BridgeError) as caught:
            self._bridge(port=1111).call("capabilities")
        self.assertEqual(caught.exception.code, ErrorCode.INVALID_CONFIGURATION)

    @patch("unreal_editor_mcp.bridge.read_token", return_value="b" * 64)
    @patch("unreal_editor_mcp.bridge.read_discovery", return_value=RECORD)
    def test_maps_bridge_error(self, _discovery, _token):
        class ErrorConnection(FakeConnection):
            def __init__(self, *args, **kwargs):
                super().__init__(*args, **kwargs)
                self.response = FakeResponse(401, json.dumps({
                    "ok": False,
                    "error": {"code": "authentication_failed", "message": "Denied", "details": {}, "retryable": False},
                }).encode())
        with self.assertRaises(BridgeError) as caught:
            self._bridge(ErrorConnection).call("capabilities")
        self.assertEqual(caught.exception.code, ErrorCode.AUTHENTICATION_FAILED)

    @patch("unreal_editor_mcp.bridge.read_token", return_value="b" * 64)
    @patch("unreal_editor_mcp.bridge.read_discovery", return_value=RECORD)
    def test_rejects_oversized_and_invalid_responses(self, _discovery, _token):
        bodies = [b"x" * (MAX_RESPONSE_BYTES + 1), b"not json", b'{"ok":true}']
        for body in bodies:
            class BadConnection(FakeConnection):
                def __init__(self, *args, **kwargs):
                    super().__init__(*args, **kwargs)
                    self.response = FakeResponse(200, body)
            with self.subTest(length=len(body)), self.assertRaises(BridgeError):
                self._bridge(BadConnection).call("capabilities")

    @patch("unreal_editor_mcp.bridge.read_token", return_value="b" * 64)
    @patch("unreal_editor_mcp.bridge.read_discovery", return_value=RECORD)
    def test_timeout_and_close_are_bounded(self, _discovery, _token):
        class TimeoutConnection(FakeConnection):
            def getresponse(self):
                raise TimeoutError
        with self.assertRaises(BridgeError) as caught:
            self._bridge(TimeoutConnection).call("capabilities")
        self.assertEqual(caught.exception.code, ErrorCode.TIMEOUT)
        with self.assertRaises(BridgeError) as caught:
            self._bridge(TimeoutConnection).call("blueprint_save", {
                "operation_id": "c" * 32,
                "asset_path": "/Game/BP_Test.BP_Test",
                "expected_snapshot": "d" * 40,
            })
        self.assertEqual(caught.exception.code, ErrorCode.OUTCOME_UNKNOWN)
        self.assertEqual(caught.exception.details["operation_id"], "c" * 32)
        bridge = self._bridge()
        bridge.close()
        with self.assertRaises(BridgeError) as caught:
            bridge.call("capabilities")
        self.assertEqual(caught.exception.code, ErrorCode.CANCELLED)
