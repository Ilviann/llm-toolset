from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from rooted_files_mcp.filesystem import RootedFilesystem
from rooted_files_mcp.server import MCPServer


class MCPServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.server = MCPServer(RootedFilesystem(self.root))

    def tearDown(self) -> None:
        self.temp.cleanup()

    def request(self, method: str, params: dict | None = None) -> dict:
        result = self.server.handle({
            "jsonrpc": "2.0", "id": 1, "method": method, "params": params or {}
        })
        assert result is not None
        return result

    def test_initialize_and_list_tools(self) -> None:
        initialized = self.request("initialize", {"protocolVersion": "2025-06-18"})
        self.assertEqual(initialized["result"]["protocolVersion"], "2025-06-18")
        tools = self.request("tools/list")["result"]["tools"]
        self.assertEqual([tool["name"] for tool in tools], [
            "list_dir", "tree", "read_text", "write_text"
        ])

    def test_tool_error_uses_mcp_tool_result(self) -> None:
        response = self.request("tools/call", {
            "name": "read_text", "arguments": {"path": "../missing"}
        })
        self.assertTrue(response["result"]["isError"])

    def test_notification_has_no_response(self) -> None:
        response = self.server.handle({
            "jsonrpc": "2.0", "method": "notifications/initialized"
        })
        self.assertIsNone(response)


if __name__ == "__main__":
    unittest.main()

