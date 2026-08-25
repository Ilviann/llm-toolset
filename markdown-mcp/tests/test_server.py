from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from markdown_mcp import __version__
from markdown_mcp.configuration import Settings
from markdown_mcp.filesystem import MarkdownFilesystem
from markdown_mcp.server import MCPServer, build_tools


class ServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "guide.md").write_text(
            "---\ntitle: Test\n---\n# Intro\nbody\n",
            encoding="utf-8",
            newline="\n",
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def server(self, writable: bool = False) -> MCPServer:
        settings = Settings.for_root(self.root, writable=writable)
        return MCPServer(MarkdownFilesystem(settings), settings)

    @staticmethod
    def request(method: str, params: object | None = None) -> dict[str, object]:
        message: dict[str, object] = {
            "jsonrpc": "2.0",
            "id": 1,
            "method": method,
        }
        if params is not None:
            message["params"] = params
        return message

    def test_catalogs_are_exact_and_schemas_are_closed(self) -> None:
        readonly = build_tools(Settings.for_root(self.root))
        writable = build_tools(Settings.for_root(self.root, writable=True))
        self.assertEqual(
            [tool["name"] for tool in readonly],
            ["read_markdown", "list_sections"],
        )
        self.assertEqual(
            [tool["name"] for tool in writable],
            [
                "read_markdown",
                "list_sections",
                "overwrite_section",
                "append_section",
                "set_front_matter",
                "delete_section",
            ],
        )
        for tool in writable:
            self.assertFalse(tool["inputSchema"]["additionalProperties"])
        self.assertIn("outputSchema", writable[1])
        self.assertEqual(
            len(json.dumps(readonly, separators=(",", ":")).encode("utf-8")),
            923,
        )
        self.assertEqual(
            len(json.dumps(writable, separators=(",", ":")).encode("utf-8")),
            2092,
        )

    def test_package_and_python_versions_are_synchronized(self) -> None:
        pyproject = (Path(__file__).parents[1] / "pyproject.toml").read_text(
            encoding="utf-8"
        )
        self.assertIn(f'version = "{__version__}"', pyproject)
        self.assertIn('requires-python = ">=3.10"', pyproject)

    def test_initialize_ping_notifications_and_protocol_errors(self) -> None:
        server = self.server()
        response = server.handle(
            self.request(
                "initialize", {"protocolVersion": "2025-06-18"}
            )
        )
        assert response is not None
        self.assertEqual(response["result"]["protocolVersion"], "2025-06-18")
        self.assertEqual(
            response["result"]["serverInfo"],
            {
                "name": "markdown-mcp",
                "version": __version__,
                "description": "Root-confined Markdown section tools",
            },
        )
        self.assertEqual(server.handle(self.request("ping"))["result"], {})
        self.assertIsNone(
            server.handle({"jsonrpc": "2.0", "method": "notifications/initialized"})
        )
        invalid = server.handle({"jsonrpc": "1.0", "id": 2, "method": "ping"})
        self.assertEqual(invalid["error"]["code"], -32600)
        missing = server.handle(self.request("missing"))
        self.assertEqual(missing["error"]["code"], -32601)

    def test_disabled_direct_tool_call_is_rejected(self) -> None:
        response = self.server().handle(
            self.request(
                "tools/call",
                {
                    "name": "delete_section",
                    "arguments": {"path": "guide.md#intro"},
                },
            )
        )
        result = response["result"]
        self.assertTrue(result["isError"])
        self.assertIn("read-only", result["content"][0]["text"])

    def test_read_list_and_writable_tool_calls(self) -> None:
        server = self.server(writable=True)
        read = server.handle(
            self.request(
                "tools/call",
                {
                    "name": "read_markdown",
                    "arguments": {"path": "guide.md#intro"},
                },
            )
        )
        self.assertEqual(read["result"]["content"][0]["text"], "# Intro\nbody\n")

        listed = server.handle(
            self.request(
                "tools/call",
                {
                    "name": "list_sections",
                    "arguments": {"path": "guide.md"},
                },
            )
        )
        self.assertEqual(
            listed["result"]["structuredContent"],
            {
                "has_front_matter": True,
                "sections": [{"level": 1, "title": "Intro", "anchor": "intro"}],
            },
        )
        appended = server.handle(
            self.request(
                "tools/call",
                {
                    "name": "append_section",
                    "arguments": {
                        "path": "guide.md#intro",
                        "title": "Child",
                        "body": "new",
                    },
                },
            )
        )
        self.assertFalse(appended["result"].get("isError", False))
        self.assertIn("## Child\nnew", (self.root / "guide.md").read_text(encoding="utf-8"))

    def test_argument_errors_are_tool_errors(self) -> None:
        cases = (
            ({"name": "read_markdown", "arguments": {}}, "Missing argument"),
            (
                {
                    "name": "read_markdown",
                    "arguments": {"path": "guide.md", "extra": 1},
                },
                "Unexpected argument",
            ),
            (
                {
                    "name": "list_sections",
                    "arguments": {"path": "guide.md", "max_level": True},
                },
                "max_level",
            ),
        )
        for params, message in cases:
            with self.subTest(params=params):
                response = self.server().handle(self.request("tools/call", params))
                self.assertTrue(response["result"]["isError"])
                self.assertIn(message, response["result"]["content"][0]["text"])

        invalid = self.server().handle(
            self.request("tools/call", {"name": "read_markdown", "arguments": []})
        )
        self.assertEqual(invalid["error"]["code"], -32602)


if __name__ == "__main__":
    unittest.main()
