from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from rooted_files_mcp.filesystem import FileAccessError
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
        self.assertEqual(
            initialized["result"]["serverInfo"]["version"], "0.3.0"
        )
        tools = self.request("tools/list")["result"]["tools"]
        self.assertEqual([tool["name"] for tool in tools], [
            "list_dir", "tree", "read_text", "write_text", "write_lines",
        ])
        by_name = {tool["name"]: tool for tool in tools}
        for name in ("read_text", "write_lines"):
            schema = by_name[name]["inputSchema"]
            self.assertEqual(schema["properties"]["start_line"]["type"], "integer")
            self.assertEqual(schema["properties"]["start_line"]["minimum"], 1)
            self.assertEqual(schema["properties"]["end_line"]["minimum"], 1)
            self.assertIn("one-based", by_name[name]["description"])
            self.assertIn("end-inclusive", by_name[name]["description"])
        self.assertEqual(by_name["read_text"]["inputSchema"]["required"], ["path"])

    def test_tool_error_uses_mcp_tool_result(self) -> None:
        response = self.request("tools/call", {
            "name": "read_text", "arguments": {"path": "../missing"}
        })
        self.assertTrue(response["result"]["isError"])

    def test_disabled_read_tools_are_omitted_and_rejected(self) -> None:
        settings = replace(self.server.settings, read=False)
        server = MCPServer(RootedFilesystem(settings), settings)
        self.server = server
        tools = self.request("tools/list")["result"]["tools"]
        self.assertEqual(
            [tool["name"] for tool in tools], ["write_text", "write_lines"]
        )
        response = self.request("tools/call", {
            "name": "read_text", "arguments": {"path": "missing.txt"}
        })
        self.assertTrue(response["result"]["isError"])
        self.assertIn("disabled", response["result"]["content"][0]["text"])
        with self.assertRaisesRegex(FileAccessError, "Read access is disabled"):
            server.fs.list_dir()

    def test_disabled_write_tool_is_omitted_and_rejected(self) -> None:
        settings = replace(self.server.settings, write=False)
        server = MCPServer(RootedFilesystem(settings), settings)
        self.server = server
        tools = self.request("tools/list")["result"]["tools"]
        self.assertEqual([tool["name"] for tool in tools], [
            "list_dir", "tree", "read_text"
        ])
        response = self.request("tools/call", {
            "name": "write_text",
            "arguments": {"path": "note.txt", "content": "no"},
        })
        self.assertTrue(response["result"]["isError"])
        self.assertFalse((self.root / "note.txt").exists())
        with self.assertRaisesRegex(FileAccessError, "Write access is disabled"):
            server.fs.write_text("note.txt", "no")

    def test_write_only_mode_can_replace_validated_text(self) -> None:
        (self.root / "note.txt").write_text("old", encoding="utf-8")
        settings = replace(self.server.settings, read=False, write=True)
        server = MCPServer(RootedFilesystem(settings), settings)
        response = server.handle({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {
                "name": "write_text",
                "arguments": {"path": "note.txt", "content": "new"},
            },
        })
        assert response is not None
        self.assertNotIn("isError", response["result"])
        self.assertEqual((self.root / "note.txt").read_text(encoding="utf-8"), "new")

    def test_optional_read_range_and_line_write_calls(self) -> None:
        target = self.root / "lines.txt"
        target.write_text("one\ntwo\nthree\n", encoding="utf-8")
        read = self.request("tools/call", {
            "name": "read_text",
            "arguments": {"path": "lines.txt", "start_line": 2, "end_line": 2},
        })
        self.assertEqual(read["result"]["content"][0]["text"], "two\n")

        written = self.request("tools/call", {
            "name": "write_lines",
            "arguments": {
                "path": "lines.txt",
                "start_line": 2,
                "end_line": 2,
                "content": "changed",
            },
        })
        self.assertNotIn("isError", written["result"])
        self.assertEqual(
            target.read_text(encoding="utf-8"), "one\nchanged\nthree\n"
        )

        incomplete = self.request("tools/call", {
            "name": "read_text",
            "arguments": {"path": "lines.txt", "start_line": 1},
        })
        self.assertTrue(incomplete["result"]["isError"])
        self.assertEqual(
            incomplete["result"]["content"][0]["text"],
            "Start line and end line must be provided together",
        )

        removed = self.request("tools/call", {
            "name": "read_lines",
            "arguments": {"path": "lines.txt", "start_line": 1, "end_line": 1},
        })
        self.assertEqual(removed["error"]["code"], -32602)
        self.assertEqual(removed["error"]["message"], "Unknown tool")

    def test_notification_has_no_response(self) -> None:
        response = self.server.handle({
            "jsonrpc": "2.0", "method": "notifications/initialized"
        })
        self.assertIsNone(response)


class StdioStartupTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.script = Path(__file__).resolve().parents[1] / "server.py"

    def tearDown(self) -> None:
        self.temp.cleanup()

    @staticmethod
    def message(method: str, params: dict | None = None, request_id: int = 1) -> str:
        return json.dumps({
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
            "params": params or {},
        }) + "\n"

    def launch(self, *args: str, stdin: str = "") -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(self.script), *args],
            input=stdin,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_legacy_positional_root_stdio_launch(self) -> None:
        root = self.base / "legacy root"
        root.mkdir()
        (root / "hello.txt").write_text("hello", encoding="utf-8")
        stdin = self.message("initialize", {"protocolVersion": "2025-06-18"})
        stdin += self.message("tools/list", request_id=2)
        stdin += self.message("tools/call", {
            "name": "read_text", "arguments": {"path": "hello.txt"}
        }, request_id=3)
        stdin += self.message("tools/call", {
            "name": "write_lines",
            "arguments": {
                "path": "hello.txt",
                "start_line": 1,
                "end_line": 1,
                "content": "updated",
            },
        }, request_id=4)
        stdin += self.message("tools/call", {
            "name": "read_text",
            "arguments": {"path": "hello.txt", "start_line": 1, "end_line": 1},
        }, request_id=5)
        result = self.launch(str(root), stdin=stdin)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, "")
        responses = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(responses), 5)
        self.assertEqual(responses[2]["result"]["content"][0]["text"], "hello")
        self.assertNotIn("isError", responses[3]["result"])
        self.assertEqual(responses[4]["result"]["content"][0]["text"], "updated")

    def test_configuration_only_stdio_launch_has_clean_stdout(self) -> None:
        workspace = self.base / "configured workspace"
        exposed = workspace / "files"
        config_dir = workspace / ".mcp"
        exposed.mkdir(parents=True)
        config_dir.mkdir()
        (exposed / "hello.txt").write_text("hello", encoding="utf-8")
        (config_dir / "rooted-files-mcp.ini").write_text(
            "[paths]\nroot = files\n[permissions]\nwrite = true\n",
            encoding="utf-8",
        )
        stdin = self.message("tools/list")
        stdin += self.message("tools/call", {
            "name": "write_text",
            "arguments": {"path": "new.txt", "content": "no"},
        }, request_id=2)
        result = self.launch(
            "--workspace", str(workspace), "--no-write", stdin=stdin
        )
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, "")
        responses = [json.loads(line) for line in result.stdout.splitlines()]
        names = [tool["name"] for tool in responses[0]["result"]["tools"]]
        self.assertEqual(names, ["list_dir", "tree", "read_text"])
        self.assertTrue(responses[1]["result"]["isError"])
        self.assertFalse((exposed / "new.txt").exists())

    def test_mcp_configuration_is_never_exposed_by_file_tools(self) -> None:
        workspace = self.base / "protected workspace"
        config_dir = workspace / ".mcp"
        config_dir.mkdir(parents=True)
        (workspace / "hello.txt").write_text("hello", encoding="utf-8")
        config_path = config_dir / "rooted-files-mcp.ini"
        original = "[paths]\nroot = .\n[features]\nshow_hidden = true\n"
        config_path.write_text(original, encoding="utf-8")

        stdin = self.message("tools/call", {
            "name": "list_dir", "arguments": {"path": "."}
        })
        stdin += self.message("tools/call", {
            "name": "read_text",
            "arguments": {"path": ".mcp/rooted-files-mcp.ini"},
        }, request_id=2)
        stdin += self.message("tools/call", {
            "name": "write_text",
            "arguments": {
                "path": ".mcp/rooted-files-mcp.ini",
                "content": "changed",
            },
        }, request_id=3)
        stdin += self.message("tools/call", {
            "name": "read_text",
            "arguments": {
                "path": ".mcp/rooted-files-mcp.ini",
                "start_line": 1,
                "end_line": 1,
            },
        }, request_id=4)
        stdin += self.message("tools/call", {
            "name": "write_lines",
            "arguments": {
                "path": ".mcp/rooted-files-mcp.ini",
                "start_line": 1,
                "end_line": 1,
                "content": "changed",
            },
        }, request_id=5)
        result = self.launch("--workspace", str(workspace), stdin=stdin)

        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, "")
        responses = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(responses[0]["result"]["content"][0]["text"], "hello.txt")
        for response in responses[1:]:
            self.assertTrue(response["result"]["isError"])
            self.assertEqual(
                response["result"]["content"][0]["text"],
                "Hidden path access is denied",
            )
        self.assertEqual(config_path.read_text(encoding="utf-8"), original)

    def test_startup_error_writes_only_to_stderr(self) -> None:
        workspace = self.base / "bad workspace"
        config_dir = workspace / ".mcp"
        config_dir.mkdir(parents=True)
        (config_dir / "rooted-files-mcp.ini").write_text(
            "not an ini", encoding="utf-8"
        )
        result = self.launch("--workspace", str(workspace))
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("Malformed configuration", result.stderr)

    def test_removed_line_access_cli_options_are_rejected(self) -> None:
        root = self.base / "removed options"
        root.mkdir()
        for option in ("--line-access", "--no-line-access"):
            with self.subTest(option=option):
                result = self.launch(str(root), option)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")
                self.assertIn("unrecognized arguments", result.stderr)


if __name__ == "__main__":
    unittest.main()
