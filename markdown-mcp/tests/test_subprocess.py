from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


APP_ROOT = Path(__file__).resolve().parents[1]
SERVER = APP_ROOT / "server.py"


class SubprocessTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def message(identifier: int, method: str, params: object | None = None) -> str:
        value: dict[str, object] = {
            "jsonrpc": "2.0",
            "id": identifier,
            "method": method,
        }
        if params is not None:
            value["params"] = params
        return json.dumps(value, ensure_ascii=False) + "\n"

    def launch(
        self,
        *arguments: str,
        stdin: str,
        env: dict[str, str] | None = None,
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(SERVER), *arguments],
            input=stdin,
            text=True,
            encoding="utf-8",
            capture_output=True,
            cwd=APP_ROOT,
            env=env,
            timeout=10,
            check=False,
        )

    def test_lm_studio_initialization_list_and_call_framing(self) -> None:
        (self.root / "guide.md").write_text(
            "# Guide\nbody\n", encoding="utf-8", newline="\n"
        )
        stdin = (
            self.message(1, "initialize", {"protocolVersion": "2024-11-05"})
            + json.dumps(
                {"jsonrpc": "2.0", "method": "notifications/initialized"}
            )
            + "\n"
            + self.message(2, "tools/list")
            + self.message(
                3,
                "tools/call",
                {
                    "name": "read_markdown",
                    "arguments": {"path": "guide.md#guide"},
                },
            )
        )
        result = self.launch(str(self.root), stdin=stdin)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        responses = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([item["id"] for item in responses], [1, 2, 3])
        self.assertEqual(responses[0]["result"]["protocolVersion"], "2024-11-05")
        self.assertEqual(
            [tool["name"] for tool in responses[1]["result"]["tools"]],
            ["read_markdown", "list_sections"],
        )
        self.assertEqual(
            responses[2]["result"]["content"][0]["text"], "# Guide\nbody\n"
        )

    def test_writable_catalog_and_unicode_survive_ascii_inherited_encoding(self) -> None:
        expected = "# Unicode\narrow → Привет\n"
        (self.root / "unicode.md").write_text(expected, encoding="utf-8", newline="\n")
        env = os.environ.copy()
        env["PYTHONIOENCODING"] = "ascii:strict"
        stdin = self.message(1, "tools/list") + self.message(
            2,
            "tools/call",
            {
                "name": "read_markdown",
                "arguments": {"path": "unicode.md"},
            },
        )
        result = self.launch(str(self.root), "--writable", stdin=stdin, env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        responses = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(responses[0]["result"]["tools"]), 6)
        self.assertEqual(responses[1]["result"]["content"][0]["text"], expected)

    def test_invalid_json_is_bounded_and_process_continues(self) -> None:
        stdin = "{bad json\n" + self.message(2, "ping")
        result = self.launch(str(self.root), stdin=stdin)
        self.assertEqual(result.returncode, 0, result.stderr)
        responses = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(responses[0]["error"]["code"], -32700)
        self.assertEqual(responses[1]["result"], {})


if __name__ == "__main__":
    unittest.main()
