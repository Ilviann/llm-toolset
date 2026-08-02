from __future__ import annotations

import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from scripts import deploy_plugin as deploy


class GodotDeploymentScriptTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary.name)
        self.project_root = self.base / "project with spaces"
        self.project_root.mkdir()
        self.descriptor = self.project_root / "project.godot"
        self.descriptor.write_text(
            'config_version=5\n\n[application]\nconfig/name="Deployment Test"\n',
            encoding="utf-8",
        )
        self.project = deploy.locate_project(self.project_root)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_locate_project_requires_regular_bounded_descriptor(self) -> None:
        empty = self.base / "empty"
        empty.mkdir()
        with self.assertRaisesRegex(deploy.DeploymentError, "project.godot"):
            deploy.locate_project(empty)
        self.descriptor.write_bytes(b"x" * (deploy.MAX_PROJECT_BYTES + 1))
        with self.assertRaisesRegex(deploy.DeploymentError, "larger than 4 MiB"):
            deploy.locate_project(self.project_root)

    def test_enable_plugin_adds_section_and_preserves_crlf_and_bom(self) -> None:
        original = 'config_version=5\r\n\r\n[application]\r\nconfig/name="Game"\r\n'
        updated, changed = deploy.enable_plugin_text(original)
        self.assertTrue(changed)
        self.assertIn(
            '[editor_plugins]\r\nenabled=PackedStringArray('
            '"res://addons/godot_mcp/plugin.cfg")\r\n',
            updated,
        )
        self.assertNotIn("\n", updated.replace("\r\n", ""))

    def test_enable_plugin_appends_to_existing_plugins_without_duplication(self) -> None:
        original = (
            "[editor_plugins]\n"
            'enabled=PackedStringArray("res://addons/other/plugin.cfg")\n'
            "\n[rendering]\nrenderer/rendering_method=\"gl_compatibility\"\n"
        )
        updated, changed = deploy.enable_plugin_text(original)
        self.assertTrue(changed)
        self.assertIn('"res://addons/other/plugin.cfg"', updated)
        self.assertIn(f'"{deploy.PLUGIN_RESOURCE}"', updated)
        unchanged, changed_again = deploy.enable_plugin_text(updated)
        self.assertFalse(changed_again)
        self.assertEqual(unchanged, updated)
        self.assertEqual(updated.count(deploy.PLUGIN_RESOURCE), 1)

    def test_enable_plugin_rejects_ambiguous_or_unsupported_configuration(self) -> None:
        with self.assertRaisesRegex(deploy.DeploymentError, "duplicate"):
            deploy.enable_plugin_text(
                "[editor_plugins]\nenabled=PackedStringArray()\n"
                "[editor_plugins]\nenabled=PackedStringArray()\n"
            )
        with self.assertRaisesRegex(deploy.DeploymentError, "PackedStringArray"):
            deploy.enable_plugin_text("[editor_plugins]\nenabled=[\"plugin.cfg\"]\n")

    def test_build_definition_defaults_to_small_and_formats_lm_studio_json(self) -> None:
        definition = deploy.build_server_definition(self.project)
        self.assertEqual(
            definition["args"],
            [
                str(deploy.SERVER_ENTRY),
                str(self.project_root.resolve()),
                "--mode",
                "small",
            ],
        )
        self.assertEqual(
            json.loads(deploy.format_mcp_json(definition)),
            {"mcpServers": {deploy.SERVER_NAME: definition}},
        )

    def test_large_definition_accepts_bounded_explicit_godot_executable(self) -> None:
        executable = self.base / ("Godot.exe" if os.name == "nt" else "Godot")
        executable.write_bytes(b"")
        definition = deploy.build_server_definition(
            self.project,
            "large",
            godot_executable=executable,
        )
        self.assertEqual(
            definition["args"][-2:],
            ["--godot-executable", str(executable.resolve())],
        )
        with self.assertRaisesRegex(deploy.DeploymentError, "only in large mode"):
            deploy.build_server_definition(
                self.project,
                "small",
                godot_executable=executable,
            )

    def test_generated_small_definition_starts_server_with_small_tools(self) -> None:
        definition = deploy.build_server_definition(self.project)
        request = json.dumps(
            {"jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": {}}
        ) + "\n"
        result = subprocess.run(
            [
                str(definition["command"]),
                *(str(value) for value in definition["args"]),
            ],
            input=request,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        response = json.loads(result.stdout)
        names = {tool["name"] for tool in response["result"]["tools"]}
        self.assertIn("list_assets", names)
        self.assertNotIn("select_node", names)

    def test_deploy_installs_and_enables_bundled_addon(self) -> None:
        result = deploy.deploy(self.project, replace_existing=False)
        self.assertEqual(result.destination, self.project_root / "addons" / "godot_mcp")
        self.assertTrue((result.destination / "plugin.cfg").is_file())
        self.assertTrue(result.plugin_enabled)
        self.assertTrue(result.project_changed)
        descriptor = self.descriptor.read_text(encoding="utf-8")
        self.assertIn(deploy.PLUGIN_RESOURCE, descriptor)
        self.assertEqual(
            deploy.plugin_manifest(result.destination),
            deploy.plugin_manifest(deploy.SOURCE_PLUGIN),
        )

    def test_replace_removes_stale_files_and_does_not_duplicate_enablement(self) -> None:
        first = deploy.deploy(self.project, replace_existing=False)
        (first.destination / "stale.txt").write_text("stale", encoding="utf-8")
        second = deploy.deploy(self.project, replace_existing=True)
        self.assertFalse((second.destination / "stale.txt").exists())
        self.assertFalse(second.project_changed)
        self.assertEqual(self.descriptor.read_text(encoding="utf-8").count(deploy.PLUGIN_RESOURCE), 1)
        self.assertEqual(list(second.destination.parent.glob(".godot_mcp.*-*")), [])

    def test_existing_addon_requires_explicit_replacement(self) -> None:
        destination = self.project_root / "addons" / "godot_mcp"
        destination.mkdir(parents=True)
        with self.assertRaisesRegex(deploy.DeploymentError, "confirm replacement"):
            deploy.deploy(self.project, replace_existing=False)
        self.assertTrue(destination.is_dir())

    def test_post_install_failure_restores_previous_addon_and_project_file(self) -> None:
        destination = self.project_root / "addons" / "godot_mcp"
        destination.mkdir(parents=True)
        (destination / "old.txt").write_text("old", encoding="utf-8")
        original_project = self.descriptor.read_bytes()
        real_manifest = deploy.plugin_manifest

        def fail_installed_manifest(path: Path) -> object:
            if path == destination:
                raise deploy.DeploymentError("injected post-install failure")
            return real_manifest(path)

        with mock.patch.object(deploy, "plugin_manifest", side_effect=fail_installed_manifest):
            with self.assertRaisesRegex(deploy.DeploymentError, "injected"):
                deploy.deploy(self.project, replace_existing=True)
        self.assertEqual((destination / "old.txt").read_text(encoding="utf-8"), "old")
        self.assertEqual(self.descriptor.read_bytes(), original_project)
        self.assertEqual(list(destination.parent.glob(".godot_mcp.*-*")), [])

    @unittest.skipUnless(hasattr(os, "symlink"), "symbolic links are unavailable")
    def test_deploy_rejects_linked_addons_boundary(self) -> None:
        outside = self.base / "outside"
        outside.mkdir()
        addons = self.project_root / "addons"
        try:
            addons.symlink_to(outside, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"symbolic links are unavailable for this account: {error}")
        with self.assertRaisesRegex(deploy.DeploymentError, "regular folder"):
            deploy.deploy(self.project, replace_existing=False)
        self.assertEqual(list(outside.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
