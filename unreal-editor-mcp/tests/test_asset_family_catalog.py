import unittest

from unreal_editor_mcp.asset_family_catalog import (
    ASSET_FAMILY_CATALOG,
    BRIDGE_HANDLER,
    JSON_RESULT,
    LIFECYCLE_HANDLER,
    MUTATION_ACCESS,
    READ_ACCESS,
    SAFE_YAML_RESULT,
    AssetFamilyPublication,
    StaticAssetFamilyCatalog,
    ToolPublication,
    compose_companion_capabilities,
)


def tool(name="fixture"):
    return {
        "name": name,
        "description": "Fixture.",
        "inputSchema": {
            "type": "object", "properties": {}, "additionalProperties": False,
        },
    }


def publication(name="fixture", *, command=None, access=READ_ACCESS):
    command = name if command is None else command
    return ToolPublication(
        tool(name), access, command, BRIDGE_HANDLER, JSON_RESULT, (command,),
    )


class AssetFamilyCatalogTests(unittest.TestCase):
    def test_static_catalog_composes_deterministically_without_mutating_schemas(self):
        before = ASSET_FAMILY_CATALOG.compose(
            writable=True, lifecycle_enabled=True,
        )
        first = ASSET_FAMILY_CATALOG.compose(
            writable=True, lifecycle_enabled=True,
        )
        second = ASSET_FAMILY_CATALOG.compose(
            writable=True, lifecycle_enabled=True,
        )
        self.assertEqual(first.tools, second.tools)
        self.assertEqual(first.tools, before.tools)
        self.assertEqual(len(first.tools), 26)
        self.assertEqual(len(first.publications), 26)

    def test_catalog_rejects_duplicate_families_tools_and_command_mappings(self):
        entry = AssetFamilyPublication("fixture", (publication(),))
        with self.assertRaisesRegex(RuntimeError, "Duplicate or empty Python asset family"):
            StaticAssetFamilyCatalog((entry, entry))
        with self.assertRaisesRegex(RuntimeError, "Duplicate or invalid public tool"):
            StaticAssetFamilyCatalog((
                entry,
                AssetFamilyPublication("other", (publication(),)),
            ))
        with self.assertRaisesRegex(RuntimeError, "Conflicting native command mapping"):
            StaticAssetFamilyCatalog((AssetFamilyPublication("fixture", (
                publication("one", command="shared"),
                publication("two", command="shared"),
            )),))

    def test_catalog_rejects_invalid_access_and_missing_bridge_mapping(self):
        with self.assertRaisesRegex(RuntimeError, "Invalid access"):
            StaticAssetFamilyCatalog((AssetFamilyPublication(
                "fixture", (publication(access="write-ish"),),
            ),))
        invalid = ToolPublication(tool(), READ_ACCESS, None)
        with self.assertRaisesRegex(RuntimeError, "no native command mapping"):
            StaticAssetFamilyCatalog((AssetFamilyPublication("fixture", (invalid,)),))

    def test_access_and_native_requirements_filter_exact_publications(self):
        readonly = ASSET_FAMILY_CATALOG.compose(
            writable=False,
            lifecycle_enabled=False,
            native_capabilities={"commands": ["capabilities", "asset_inspect"]},
        )
        self.assertEqual(
            [item["name"] for item in readonly.tools],
            ["capabilities", "asset_inspect"],
        )
        writable = ASSET_FAMILY_CATALOG.compose(
            writable=True,
            lifecycle_enabled=True,
            native_capabilities={"commands": ["capabilities", "asset_delete"]},
        )
        self.assertEqual(
            [item["name"] for item in writable.tools],
            ["capabilities", "asset_delete", "editor_lifecycle"],
        )
        malformed = ASSET_FAMILY_CATALOG.compose(
            writable=True,
            lifecycle_enabled=True,
            native_capabilities={"commands": "not-a-list"},
        )
        self.assertEqual([item["name"] for item in malformed.tools], ["editor_lifecycle"])

    def test_dispatch_and_result_policies_are_catalog_owned(self):
        composition = ASSET_FAMILY_CATALOG.compose(
            writable=True, lifecycle_enabled=True,
        )
        asset = composition.publications["asset_inspect"]
        self.assertEqual(asset.native_command, "asset_inspect")
        self.assertEqual(asset.result_handler, SAFE_YAML_RESULT)
        self.assertEqual(asset.access, READ_ACCESS)
        deletion = composition.publications["asset_delete"]
        self.assertEqual(deletion.native_command, "asset_delete")
        self.assertEqual(deletion.result_handler, JSON_RESULT)
        self.assertEqual(deletion.access, MUTATION_ACCESS)
        lifecycle = composition.publications["editor_lifecycle"]
        self.assertEqual(lifecycle.handler, LIFECYCLE_HANDLER)
        self.assertIsNone(lifecycle.native_command)

    def test_unavailable_command_does_not_change_other_schema_branches(self):
        full = ASSET_FAMILY_CATALOG.compose(
            writable=True, lifecycle_enabled=False,
        )
        narrowed = ASSET_FAMILY_CATALOG.compose(
            writable=True,
            lifecycle_enabled=False,
            native_capabilities={
                "commands": [item["name"] for item in full.tools if item["name"] != "asset_delete"],
            },
        )
        narrowed_by_name = {item["name"]: item for item in narrowed.tools}
        self.assertNotIn("asset_delete", narrowed_by_name)
        for item in full.tools:
            if item["name"] != "asset_delete":
                self.assertEqual(narrowed_by_name[item["name"]], item)

    def test_companion_capability_annotation_requires_host_api_match(self):
        native = {
            "companion_api_version": 2,
            "companions": [{
                "extension_id": "unreal-mcp-gas",
                "companion_api_version": 1,
                "schema_revision": 1,
                "ready": True,
                "contributions": [],
            }],
        }
        compose_companion_capabilities(native)
        companion = native["companions"][0]
        self.assertTrue(companion["python_known"])
        self.assertFalse(companion["effective_ready"])
        self.assertEqual(
            companion["effective_unavailable_reason"], "python_schema_mismatch",
        )

    def test_companion_capability_annotation_validates_typed_v2_family_records(self):
        family = {
            "family_id": "fixture", "native_class": "/Script/CoreUObject.Object",
            "class_policy": "exact_and_derived", "priority": 25,
            "operations": {"inspect": True, "create": False, "edit": False},
            "creation_persistence": "none", "editing_persistence": "none",
            "limits": {"records": 4}, "selector_routes": ["summary"],
            "stable_nested_identity_kinds": ["entry"],
        }
        native = {
            "companion_api_version": 2,
            "companions": [{
                "extension_id": "unreal-mcp-gas", "companion_api_version": 2,
                "schema_revision": 2, "ready": True, "asset_families": [family],
                "contributions": [],
            }],
        }
        compose_companion_capabilities(native)
        self.assertTrue(native["companions"][0]["effective_ready"])
        native["companions"][0]["asset_families"][0]["operations"]["create"] = "yes"
        compose_companion_capabilities(native)
        self.assertFalse(native["companions"][0]["effective_ready"])
        self.assertEqual(
            native["companions"][0]["effective_unavailable_reason"],
            "python_schema_mismatch",
        )
        family["operations"]["create"] = False
        native["companions"][0]["asset_families"][0] = family | {"dynamic": True}
        compose_companion_capabilities(native)
        self.assertFalse(native["companions"][0]["effective_ready"])


if __name__ == "__main__":
    unittest.main()
