from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from scripts.generate_mcp_config import (
    SERVER_NAME,
    build_server_definition,
    format_codex_toml,
    format_mcp_json,
    format_mcp_settings_preview,
)


class MCPConfigGeneratorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.base = Path(self.temp.name)
        self.root = self.base / "Markdown root"
        self.root.mkdir()
        self.python = self.base / "python executable"
        self.python.write_text("", encoding="utf-8")
        self.server = self.base / "server script.py"
        self.server.write_text("", encoding="utf-8")

    def tearDown(self) -> None:
        self.temp.cleanup()

    def build(self, *, writable: bool = False) -> dict[str, object]:
        return build_server_definition(
            self.root,
            writable=writable,
            python_executable=self.python,
            server_path=self.server,
        )

    def test_builds_read_only_launch_definition(self) -> None:
        definition = self.build()
        self.assertEqual(definition["command"], str(self.python.resolve()))
        self.assertEqual(
            definition["args"],
            [str(self.server.resolve()), str(self.root.resolve())],
        )

    def test_builds_writable_launch_definition(self) -> None:
        definition = self.build(writable=True)
        self.assertEqual(definition["args"][-1], "--writable")

    def test_formats_complete_mcp_servers_json(self) -> None:
        definition = self.build(writable=True)
        parsed = json.loads(format_mcp_json(definition))
        self.assertEqual(parsed, {"mcpServers": {SERVER_NAME: definition}})

    def test_formats_codex_config_toml_entry(self) -> None:
        definition = self.build(writable=True)
        expected_arguments = ",\n".join(
            f"  {json.dumps(argument)}" for argument in definition["args"]
        )
        self.assertEqual(
            format_codex_toml(definition),
            f"[mcp_servers.{SERVER_NAME}]\n"
            f"command = {json.dumps(definition['command'])}\n"
            "args = [\n"
            f"{expected_arguments}\n"
            "]",
        )

    def test_formats_combined_settings_preview(self) -> None:
        definition = self.build()
        preview = format_mcp_settings_preview(definition)
        self.assertIn("mcp.json (LM Studio)", preview)
        self.assertIn(format_mcp_json(definition), preview)
        self.assertIn("config.toml (Codex)", preview)
        self.assertIn(format_codex_toml(definition), preview)

    def test_codex_toml_rejects_malformed_definition(self) -> None:
        with self.assertRaisesRegex(ValueError, "string command and string args"):
            format_codex_toml({"command": "python", "args": [1]})

    def test_generated_definitions_launch_both_server_modes(self) -> None:
        request = json.dumps(
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "tools/list",
                "params": {},
            }
        ) + "\n"
        expected = {
            False: ["read_markdown", "list_sections"],
            True: [
                "read_markdown",
                "list_sections",
                "overwrite_section",
                "append_section",
                "set_front_matter",
                "delete_section",
            ],
        }
        for writable, expected_tools in expected.items():
            with self.subTest(writable=writable):
                definition = build_server_definition(self.root, writable=writable)
                arguments = definition["args"]
                self.assertIsInstance(arguments, list)
                result = subprocess.run(
                    [
                        str(definition["command"]),
                        *(str(value) for value in arguments),
                    ],
                    input=request,
                    text=True,
                    encoding="utf-8",
                    capture_output=True,
                    timeout=10,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stderr, "")
                response = json.loads(result.stdout)
                self.assertEqual(
                    [tool["name"] for tool in response["result"]["tools"]],
                    expected_tools,
                )

    def test_rejects_non_boolean_writable_value(self) -> None:
        with self.assertRaisesRegex(ValueError, "true or false"):
            build_server_definition(
                self.root,
                writable="yes",  # type: ignore[arg-type]
                python_executable=self.python,
                server_path=self.server,
            )

    def test_rejects_missing_and_non_folder_roots(self) -> None:
        missing = self.base / "missing"
        with self.assertRaisesRegex(ValueError, "Root folder is inaccessible"):
            build_server_definition(
                missing,
                python_executable=self.python,
                server_path=self.server,
            )
        with self.assertRaisesRegex(ValueError, "Root path is not a folder"):
            build_server_definition(
                self.server,
                python_executable=self.python,
                server_path=self.server,
            )

    def test_rejects_invalid_launcher_files(self) -> None:
        with self.assertRaisesRegex(ValueError, "Python executable is not a file"):
            build_server_definition(
                self.root,
                python_executable=self.root,
                server_path=self.server,
            )
        with self.assertRaisesRegex(ValueError, "Server script is inaccessible"):
            build_server_definition(
                self.root,
                python_executable=self.python,
                server_path=self.base / "missing.py",
            )


if __name__ == "__main__":
    unittest.main()
