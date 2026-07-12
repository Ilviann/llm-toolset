from __future__ import annotations

from io import StringIO
import json
import unittest

from godot_editor_mcp.server import MCPServer
from godot_editor_mcp.stdio import serve


class FakeBridge:
    def call(self, command: str, arguments: dict | None = None):
        return {"command": command, "arguments": arguments or {}}


class BrokenServer:
    def handle(self, message: dict):
        raise RuntimeError("private diagnostic")


class StdioTests(unittest.TestCase):
    def test_initialize_list_and_call_are_newline_delimited_json(self) -> None:
        requests = [
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {"protocolVersion": "2025-06-18"},
            },
            {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {"name": "editor_state", "arguments": {}},
            },
        ]
        source = StringIO("".join(json.dumps(item) + "\n" for item in requests))
        output = StringIO()
        diagnostics = StringIO()

        serve(
            MCPServer(FakeBridge()),
            input_stream=source,
            output_stream=output,
            error_stream=diagnostics,
        )

        responses = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual([response["id"] for response in responses], [1, 2, 3])
        self.assertEqual(responses[0]["result"]["protocolVersion"], "2025-06-18")
        self.assertTrue(responses[1]["result"]["tools"])
        self.assertEqual(
            json.loads(responses[2]["result"]["content"][0]["text"])["command"],
            "state",
        )
        self.assertEqual(diagnostics.getvalue(), "")

    def test_parse_and_internal_errors_keep_stdout_protocol_safe(self) -> None:
        parse_output = StringIO()
        serve(
            MCPServer(FakeBridge()),
            input_stream=StringIO("not json\n"),
            output_stream=parse_output,
            error_stream=StringIO(),
        )
        parse_response = json.loads(parse_output.getvalue())
        self.assertEqual(parse_response["error"]["code"], -32700)

        output = StringIO()
        diagnostics = StringIO()
        serve(
            BrokenServer(),
            input_stream=StringIO('{"jsonrpc":"2.0","id":1,"method":"ping"}\n'),
            output_stream=output,
            error_stream=diagnostics,
        )
        self.assertEqual(json.loads(output.getvalue())["error"]["code"], -32603)
        self.assertNotIn("private diagnostic", output.getvalue())
        self.assertIn("private diagnostic", diagnostics.getvalue())


if __name__ == "__main__":
    unittest.main()
