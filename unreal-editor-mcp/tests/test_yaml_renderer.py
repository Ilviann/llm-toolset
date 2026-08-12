import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

from unreal_editor_mcp.yaml_renderer import render_safe_yaml


ROOT = Path(__file__).resolve().parents[1]


class SafeYamlTests(unittest.TestCase):
    def test_deterministic_order_escaping_and_type_preservation(self):
        value = {
            "z": "null",
            "a": [True, None, 1, 1.5, "colon: value", "Привет\nмир"],
            "nested": {"yes": "true", "number": "01", "tag": "!!python/object"},
        }
        expected = render_safe_yaml(value)
        self.assertEqual(render_safe_yaml(value), expected)
        self.assertTrue(expected.startswith('"a":\n'))
        self.assertIn('- true\n', expected)
        self.assertIn('- null\n', expected)
        self.assertIn('- 1\n', expected)
        self.assertIn('- 1.5\n', expected)
        self.assertIn('"yes": "true"', expected)
        self.assertIn('"number": "01"', expected)
        self.assertIn('"tag": "!!python/object"', expected)
        self.assertNotIn("&", expected)

    def test_rejects_non_json_and_non_finite_values(self):
        for value in ({1, 2}, float("nan"), {"x": object()}, {1: "bad"}):
            with self.subTest(value=repr(value)):
                with self.assertRaises(ValueError):
                    render_safe_yaml(value)

    def test_stdio_forces_utf8_under_non_utf8_inherited_encoding(self):
        source = r'''
import sys
from unreal_editor_mcp.stdio import configure_standard_streams, serve
class Server:
    def handle(self, message):
        return {"jsonrpc":"2.0","id":message["id"],"result":{"text":"Ж"}}
configure_standard_streams()
serve(Server())
'''
        environment = os.environ.copy()
        environment["PYTHONIOENCODING"] = "ascii"
        completed = subprocess.run(
            [sys.executable, "-c", source],
            cwd=ROOT,
            env=environment,
            input=b'{"jsonrpc":"2.0","id":1,"method":"ping"}\n',
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
            check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr.decode("utf-8", "replace"))
        self.assertEqual(json.loads(completed.stdout.decode("utf-8"))["result"]["text"], "Ж")


if __name__ == "__main__":
    unittest.main()
