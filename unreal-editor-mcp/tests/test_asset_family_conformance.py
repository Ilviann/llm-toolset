import json
import tempfile
import unittest
from pathlib import Path

from scripts.asset_family_conformance import (
    CatalogFamilyFixture,
    CatalogToolExpectation,
    CrossProcessFamilyFixture,
    InspectionVariant,
    PackageFamilyFixture,
    verify_catalog_family,
    verify_cross_process_family,
    verify_packaged_family,
    verify_recovered_mutation,
    verify_restart_read_back,
    verify_source_package_fixture,
)
from scripts.packaging.service import read_plugin_descriptor
from scripts.unreal_tooling.plugins import BASE_PLUGIN, FIXTURE_PLUGIN
from unreal_editor_mcp.asset_family_catalog import (
    ASSET_FAMILY_CATALOG,
    JSON_RESULT,
    MUTATION_ACCESS,
    READ_ACCESS,
    SAFE_YAML_RESULT,
)


CATALOG_FIXTURES = (
    CatalogFamilyFixture("assets", (
        CatalogToolExpectation("asset_inspect", READ_ACCESS, SAFE_YAML_RESULT),
        CatalogToolExpectation("asset_references", READ_ACCESS),
        CatalogToolExpectation("asset_delete", MUTATION_ACCESS),
    )),
    CatalogFamilyFixture("blueprints", (
        CatalogToolExpectation("blueprint_action_catalog", READ_ACCESS),
        CatalogToolExpectation("blueprint_graph_edit", MUTATION_ACCESS),
        CatalogToolExpectation("blueprint_block_replace", MUTATION_ACCESS),
        CatalogToolExpectation("blueprint_create", MUTATION_ACCESS),
        CatalogToolExpectation("blueprint_compile", MUTATION_ACCESS),
        CatalogToolExpectation("blueprint_save", MUTATION_ACCESS),
        CatalogToolExpectation("blueprint_component_edit", MUTATION_ACCESS),
        CatalogToolExpectation("blueprint_default_edit", MUTATION_ACCESS),
        CatalogToolExpectation("blueprint_member_edit", MUTATION_ACCESS),
    )),
    CatalogFamilyFixture("widgets", (
        CatalogToolExpectation("widget_tree_edit", MUTATION_ACCESS),
    )),
    CatalogFamilyFixture("game-data", (
        CatalogToolExpectation("game_data_inspect", READ_ACCESS),
        CatalogToolExpectation("game_data_edit", MUTATION_ACCESS),
    )),
    CatalogFamilyFixture(
        "test-companion",
        extension_id="unreal-mcp-test",
        contributions=(
            ("blueprint_inspect", "inspect_test_asset", READ_ACCESS),
            ("blueprint_default_edit", "set_test_asset_value", MUTATION_ACCESS),
            ("blueprint_inspect", "inspect_test_component", READ_ACCESS),
            ("blueprint_component_edit", "set_test_component_value", MUTATION_ACCESS),
            ("blueprint_inspect", "inspect_test_contribution", READ_ACCESS),
            ("blueprint_default_edit", "set_test_contribution_value", MUTATION_ACCESS),
        ),
    ),
)

PACKAGE_FIXTURES = (
    PackageFamilyFixture("built-in", BASE_PLUGIN, ("UnrealMCP",)),
    PackageFamilyFixture(
        "test-companion", FIXTURE_PLUGIN, ("UnrealMCPTestCompanion",),
    ),
)


class _Bridge:
    def __init__(self, result):
        self.result = result
        self.calls = []

    def call(self, command, arguments):
        self.calls.append((command, arguments))
        return json.loads(json.dumps(self.result))


class AssetFamilyConformanceTests(unittest.TestCase):
    def test_representative_catalog_families_pass_common_gates(self):
        for fixture in CATALOG_FIXTURES:
            with self.subTest(family=fixture.family_id):
                verify_catalog_family(ASSET_FAMILY_CATALOG, fixture)

    def test_base_and_test_companion_source_packages_pass_common_gates(self):
        for fixture in PACKAGE_FIXTURES:
            with self.subTest(family=fixture.family_id):
                verify_source_package_fixture(fixture)

    def test_packaged_family_requires_exact_module_binary(self):
        fixture = PACKAGE_FIXTURES[1]
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            descriptor = read_plugin_descriptor(fixture.plugin.descriptor)
            descriptor["Installed"] = True
            (output / fixture.plugin.descriptor.name).write_text(
                json.dumps(descriptor), encoding="utf-8",
            )
            binary = output / "Binaries" / "Win64" / "UnrealEditor-UnrealMCPTestCompanion.dll"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"fixture")
            verify_packaged_family(fixture, output)
            binary.rename(binary.with_name("Unrelated.dll"))
            with self.assertRaisesRegex(AssertionError, "no binary"):
                verify_packaged_family(fixture, output)

    def test_cross_process_fixture_covers_identity_variants_and_restart(self):
        result = {
            "asset": {"path": "/Game/Fixture.Fixture", "type": "actor_blueprint"},
            "snapshot_id": "a" * 40,
            "records": [{"name": "Fixture"}],
        }
        bridge = _Bridge(result)
        fixture = CrossProcessFamilyFixture(
            "actor",
            "asset_inspect",
            {"asset_path": "/Game/Fixture.Fixture"},
            (
                (("asset", "path"), "/Game/Fixture.Fixture"),
                (("asset", "type"), "actor_blueprint"),
            ),
            (InspectionVariant({"selector": "variables/Health"}),),
        )
        preserved = {"dirty": False, "undo": 7}
        verify_cross_process_family(
            bridge, fixture, preservation_probe=lambda: dict(preserved),
        )
        verify_restart_read_back(bridge, fixture, "a" * 40)
        self.assertEqual(len(bridge.calls), 6)

    def test_recovered_mutation_requires_committed_typed_result(self):
        result = verify_recovered_mutation(
            "game-data",
            {"state": "committed", "result": {"changed_count": 2}},
            expected_fields=((('changed_count',), 2),),
        )
        self.assertEqual(result["changed_count"], 2)
        with self.assertRaisesRegex(AssertionError, "did not reconcile"):
            verify_recovered_mutation("game-data", {"state": "rejected"})


if __name__ == "__main__":
    unittest.main()
