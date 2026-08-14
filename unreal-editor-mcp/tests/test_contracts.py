import json
import re
import tomllib
import unittest
from pathlib import Path

import unreal_editor_mcp
from unreal_editor_mcp.tool_catalog import (
    READONLY_TOOL_NAMES,
    READONLY_TOOLS,
    TOOLS,
    TOOLS_WITH_LIFECYCLE,
    WRITABLE_TOOL_NAMES,
    tools_for_configuration,
)
from unreal_editor_mcp.tool_catalog_families.assets import ASSET_TOOLS
from unreal_editor_mcp.tool_catalog_families.blueprints import BLUEPRINT_TOOLS
from unreal_editor_mcp.tool_catalog_families.core import CORE_TOOLS
from unreal_editor_mcp.tool_catalog_families.game_data import GAME_DATA_TOOLS
from unreal_editor_mcp.tool_catalog_families.gameplay_framework import GAMEPLAY_TOOLS
from unreal_editor_mcp.tool_catalog_families.levels import LEVEL_TOOLS
from unreal_editor_mcp.tool_catalog_families.lifecycle import EDITOR_LIFECYCLE_TOOL
from unreal_editor_mcp.tool_catalog_families.widgets import WIDGET_TOOLS


ROOT = Path(__file__).resolve().parents[1]
NATIVE_SOURCE = ROOT / "plugin/UnrealMCP/Source"
HOST_PRIVATE = NATIVE_SOURCE / "UnrealMCP/Private"
ASSET_CORE_PRIVATE = NATIVE_SOURCE / "UnrealMCPAssetCore/Private"
ASSET_CORE_PUBLIC = NATIVE_SOURCE / "UnrealMCPAssetCore/Public"
BLUEPRINT_PRIVATE = NATIVE_SOURCE / "UnrealMCPBlueprint/Private"
UMG_PRIVATE = NATIVE_SOURCE / "UnrealMCPUMG/Private"
CONTENT_PRIVATE = NATIVE_SOURCE / "UnrealMCPContent/Private"


def native_command_source():
    paths = [
        HOST_PRIVATE / "UnrealMCPBridge.cpp",
        HOST_PRIVATE / "UnrealMCPCommandCatalog.cpp",
        ASSET_CORE_PRIVATE / "UnrealMCPAssetCoreModule.cpp",
        BLUEPRINT_PRIVATE / "UnrealMCPBlueprintModule.cpp",
        UMG_PRIVATE / "UnrealMCPUMGModule.cpp",
        CONTENT_PRIVATE / "UnrealMCPContentModule.cpp",
    ]
    return "\n".join(path.read_text(encoding="utf-8") for path in paths)


def native_host_source(root=None):
    return native_command_source()


class ReleaseContractTests(unittest.TestCase):
    def test_tool_catalog_has_one_ordered_family_assembler(self):
        assembled = (
            *CORE_TOOLS,
            *ASSET_TOOLS,
            *LEVEL_TOOLS,
            *BLUEPRINT_TOOLS,
            *WIDGET_TOOLS,
            *GAMEPLAY_TOOLS,
            *GAME_DATA_TOOLS,
        )
        self.assertEqual(TOOLS, assembled)
        self.assertEqual(TOOLS_WITH_LIFECYCLE, (*assembled, EDITOR_LIFECYCLE_TOOL))
        names = [tool["name"] for tool in assembled]
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(READONLY_TOOL_NAMES | WRITABLE_TOOL_NAMES, set(names))
        self.assertFalse(READONLY_TOOL_NAMES & WRITABLE_TOOL_NAMES)
        self.assertEqual(
            READONLY_TOOLS,
            tools_for_configuration(writable=False, lifecycle_enabled=False),
        )

    def test_versions_match_executable_metadata(self):
        project = tomllib.loads((ROOT / "pyproject.toml").read_text(encoding="utf-8"))
        plugin = json.loads((ROOT / "plugin/UnrealMCP/UnrealMCP.uplugin").read_text(encoding="utf-8"))
        header = (ASSET_CORE_PUBLIC / "UnrealMCPVersion.h").read_text(encoding="utf-8")
        native = re.search(r'Version\[\].*TEXT\("([^"]+)"\)', header)
        self.assertIsNotNone(native)
        versions = {project["project"]["version"], plugin["VersionName"], native.group(1), unreal_editor_mcp.__version__}
        self.assertEqual(versions, {"0.48.0"})

    def test_companion_api_and_companion_versions_are_internally_consistent(self):
        base = json.loads((ROOT / "plugin/UnrealMCP/UnrealMCP.uplugin").read_text(encoding="utf-8"))
        fixture = json.loads((ROOT / "plugin/UnrealMCPTestCompanion/UnrealMCPTestCompanion.uplugin").read_text(encoding="utf-8"))
        gas = json.loads((ROOT / "plugin/UnrealMCPGAS/UnrealMCPGAS.uplugin").read_text(encoding="utf-8"))
        commonui = json.loads((ROOT / "plugin/UnrealMCPCommonUI/UnrealMCPCommonUI.uplugin").read_text(encoding="utf-8"))
        base_header = (ASSET_CORE_PUBLIC / "UnrealMCPVersion.h").read_text(encoding="utf-8")
        fixture_header = (ROOT / "plugin/UnrealMCPTestCompanion/Source/UnrealMCPTestCompanion/Public/UnrealMCPTestCompanionVersion.h").read_text(encoding="utf-8")
        gas_header = (ROOT / "plugin/UnrealMCPGAS/Source/UnrealMCPGAS/Public/UnrealMCPGASVersion.h").read_text(encoding="utf-8")
        commonui_header = (ROOT / "plugin/UnrealMCPCommonUI/Source/UnrealMCPCommonUI/Public/UnrealMCPCommonUIVersion.h").read_text(encoding="utf-8")
        base_api = re.search(r"CompanionApiVersion\s*=\s*(\d+)", base_header)
        fixture_api = re.search(r"CompanionApiVersion\s*=\s*(\d+)", fixture_header)
        gas_api = re.search(r"CompanionApiVersion\s*=\s*(\d+)", gas_header)
        commonui_api = re.search(r"CompanionApiVersion\s*=\s*(\d+)", commonui_header)
        fixture_version = re.search(r'Version\[\].*TEXT\("([^"]+)"\)', fixture_header)
        gas_version = re.search(r'Version\[\].*TEXT\("([^"]+)"\)', gas_header)
        commonui_version = re.search(r'Version\[\].*TEXT\("([^"]+)"\)', commonui_header)
        self.assertIsNotNone(base_api)
        self.assertIsNotNone(fixture_api)
        self.assertIsNotNone(gas_api)
        self.assertIsNotNone(commonui_api)
        self.assertIsNotNone(fixture_version)
        self.assertIsNotNone(gas_version)
        self.assertIsNotNone(commonui_version)
        self.assertEqual({base["companion_api_version"], fixture["companion_api_version"],
                          gas["companion_api_version"], commonui["companion_api_version"],
                          int(base_api.group(1)), int(fixture_api.group(1)),
                     int(gas_api.group(1)), int(commonui_api.group(1))}, {2})
        self.assertEqual(fixture["VersionName"], fixture_version.group(1))
        self.assertEqual(gas["VersionName"], gas_version.group(1))
        self.assertEqual(commonui["VersionName"], commonui_version.group(1))
        self.assertNotEqual(fixture["VersionName"], base["VersionName"])
        self.assertNotEqual(gas["VersionName"], base["VersionName"])
        self.assertNotEqual(commonui["VersionName"], base["VersionName"])
        self.assertEqual(base["Modules"][0]["LoadingPhase"], "PostEngineInit")
        self.assertEqual(fixture["Modules"][0]["LoadingPhase"], "None")
        self.assertEqual(gas["Modules"][0]["LoadingPhase"], "None")
        self.assertEqual(commonui["Modules"][0]["LoadingPhase"], "None")
        self.assertEqual(fixture["unreal_mcp_companion"]["schema_revision"], 2)
        self.assertEqual(gas["unreal_mcp_companion"]["schema_revision"], 2)
        self.assertEqual(commonui["unreal_mcp_companion"]["schema_revision"], 2)

        registry = (
            ROOT
            / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPExtensionRegistry.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("LoadModuleWithFailureReason", registry)

    def test_gas_companion_is_read_only_bounded_and_keeps_base_gas_free(self):
        base_build = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/UnrealMCP.Build.cs").read_text(encoding="utf-8")
        inspection_query = (BLUEPRINT_PRIVATE / "UnrealMCPBlueprintInspectionQuery.h").read_text(encoding="utf-8")
        inspection_support = (BLUEPRINT_PRIVATE / "UnrealMCPBlueprintInspectionSupport.h").read_text(encoding="utf-8")
        gas_build = (ROOT / "plugin/UnrealMCPGAS/Source/UnrealMCPGAS/UnrealMCPGAS.Build.cs").read_text(encoding="utf-8")
        gas_source = "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted((ROOT / "plugin/UnrealMCPGAS/Source/UnrealMCPGAS/Private").glob("*.cpp"))
        )
        self.assertNotIn("GameplayAbilities", base_build)
        for dependency in ["GameplayAbilities", "GameplayTags", "GameplayTasks"]:
            self.assertIn(f'"{dependency}"', gas_build)
        for contract in [
            "gameplay_ability", "gameplay_ability_policies", "gameplay_ability_tags",
            "gameplay_ability_triggers", "gameplay_ability_effects",
            "gameplay_effect", "gameplay_effect_duration", "gameplay_effect_modifiers",
            "gameplay_effect_executions", "gameplay_effect_stacking", "gameplay_effect_cues",
            "gameplay_effect_tags", "gameplay_effect_granted_abilities",
            "gameplay_effect_additional_effects", "gameplay_effect_requirements",
            "gameplay_effect_components", "gameplay_effect_relationships",
            "MaxTagsPerContainer", "MaxAbilityTriggers",
            "Capabilities.bInspection", "InspectionAdapter", "AssetFamilies",
        ]:
            self.assertIn(contract, gas_source)
        self.assertNotIn("EUnrealMCPExtensionAccess::Mutation", gas_source)
        self.assertIn('TEXT("gameplay_effect")', inspection_query)
        self.assertIn('TEXT("gameplay_effect")', inspection_support)

    def test_gameplay_tag_properties_are_base_owned_bounded_and_covered(self):
        blueprint_build = (
            ROOT / "plugin/UnrealMCP/Source/UnrealMCPBlueprint/UnrealMCPBlueprint.Build.cs"
        ).read_text(encoding="utf-8")
        content_build = (
            ROOT / "plugin/UnrealMCP/Source/UnrealMCPContent/UnrealMCPContent.Build.cs"
        ).read_text(encoding="utf-8")
        host_build = (
            ROOT / "plugin/UnrealMCP/Source/UnrealMCP/UnrealMCP.Build.cs"
        ).read_text(encoding="utf-8")
        adapter = (BLUEPRINT_PRIVATE / "UnrealMCPGameplayTagValueCodec.cpp").read_text(encoding="utf-8")
        property_codec = (BLUEPRINT_PRIVATE / "UnrealMCPPropertyCodec.cpp").read_text(encoding="utf-8")
        game_data_codec = (BLUEPRINT_PRIVATE / "UnrealMCPGameDataValueCodec.cpp").read_text(encoding="utf-8")
        structured_inspection = (
            BLUEPRINT_PRIVATE / "UnrealMCPStructuredDataInspection.cpp"
        ).read_text(encoding="utf-8")
        module = (BLUEPRINT_PRIVATE / "UnrealMCPBlueprintModule.cpp").read_text(encoding="utf-8")
        native_test = (
            BLUEPRINT_PRIVATE / "Tests/UnrealMCPAutomationTestsGameplayTagProperties.cpp"
        ).read_text(encoding="utf-8")
        lifecycle = (ROOT / "scripts/headless_integration/lifecycle.py").read_text(encoding="utf-8")
        self.assertIn('"GameplayTags"', blueprint_build)
        self.assertIn('"GameplayTags"', content_build)
        for dependency in ['"GameplayAbilities"', '"GameplayTasks"']:
            self.assertNotIn(dependency, blueprint_build)
            self.assertNotIn(dependency, content_build)
            self.assertNotIn(dependency, host_build)
        for contract in [
            "FGameplayTag::StaticStruct", "FGameplayTagContainer::StaticStruct",
            "IsValidGameplayTagString", "RequestGameplayTag", "MaxGameplayTagChars",
            "MaxGameplayTagsPerContainer", "CreateFromArray",
        ]:
            self.assertIn(contract, adapter)
        self.assertIn("GameplayTagValueCodec::Encode", property_codec)
        self.assertIn("GameplayTagValueCodec::Decode", property_codec)
        self.assertIn("GameplayTagValueCodec::Encode", game_data_codec)
        self.assertIn("GameplayTagValueCodec::Decode", game_data_codec)
        self.assertIn("GameplayTagValueCodec::Classify", structured_inspection)
        self.assertIn('TEXT("gameplay_tag_properties")', module)
        self.assertIn("UnrealMCP.GameplayTagProperties.CodecValidation", native_test)
        self.assertIn("UnrealMCP.GameplayTagProperties.BlueprintDefaultsAndComponents", native_test)
        self.assertIn("Config\" / \"Tags\" / \"UnrealMCPTests.ini", lifecycle)
        self.assertIn('Tag="UnrealMCP.Test.Child"', lifecycle)

    def test_commonui_companion_is_read_only_bounded_and_keeps_base_commonui_free(self):
        base_build = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/UnrealMCP.Build.cs").read_text(encoding="utf-8")
        inspection_query = (BLUEPRINT_PRIVATE / "UnrealMCPBlueprintInspectionQuery.h").read_text(encoding="utf-8")
        inspection_support = (BLUEPRINT_PRIVATE / "UnrealMCPBlueprintInspectionSupport.h").read_text(encoding="utf-8")
        commonui_build = (ROOT / "plugin/UnrealMCPCommonUI/Source/UnrealMCPCommonUI/UnrealMCPCommonUI.Build.cs").read_text(encoding="utf-8")
        commonui_source = "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted((ROOT / "plugin/UnrealMCPCommonUI/Source/UnrealMCPCommonUI/Private").glob("*.cpp"))
        )
        self.assertNotIn('"CommonUI"', base_build)
        for dependency in ["CommonUI", "CommonInput", "UMG", "UMGEditor"]:
            self.assertIn(f'"{dependency}"', commonui_build)
        for contract in [
            "commonui_widget", "commonui_activation", "commonui_references",
            "UCommonUserWidget", "UCommonActivatableWidget", "ActionDomainOverride",
            "MaxInspectionRecords", "MaxInspectedProperties",
            "Capabilities.bInspection", "InspectionAdapter", "AssetFamilies",
            "WBP_InspectionFixture",
        ]:
            self.assertIn(contract, commonui_source)
        self.assertNotIn("EUnrealMCPExtensionAccess::Mutation", commonui_source)
        for section in ["commonui_widget", "commonui_activation", "commonui_references"]:
            self.assertIn(f'TEXT("{section}")', inspection_query)
            self.assertIn(f'TEXT("{section}")', inspection_support)

    def test_public_companion_api_does_not_expose_bridge_or_credentials(self):
        api = (
            ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Public/UnrealMCPCompanionApi.h"
        ).read_text(encoding="utf-8")
        public = "\n".join(
            path.read_text(encoding="utf-8")
            for path in [
                ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Public/IUnrealMCPModule.h",
                ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Public/UnrealMCPCompanionApi.h",
            ]
        )
        for forbidden in ["HttpServer", "TokenStore", "Credential", "FUnrealMCPBridge"]:
            self.assertNotIn(forbidden, public)
        for required in ["RegisterCompanion", "UnregisterCompanion", "GetCompanionApiVersion"]:
            self.assertIn(required, public)
        self.assertNotIn("FJsonObject", api)
        for required in [
            "FUnrealMCPRecord", "FUnrealMCPCompanionAssetFamily",
            "FUnrealMCPAssetFamilySelectorRoute", "StableNestedIdentityKinds",
            "CreationPersistence", "EditingPersistence", "InspectionAdapter",
            "CreationAdapter", "EditingAdapter", "SnapshotBuilder",
        ]:
            self.assertIn(required, api)
        for companion in ["UnrealMCPGAS", "UnrealMCPCommonUI", "UnrealMCPTestCompanion"]:
            root = ROOT / "plugin" / companion / "Source" / companion
            source = "\n".join(
                path.read_text(encoding="utf-8") for path in sorted(root.rglob("*.cpp"))
            )
            build = (root / f"{companion}.Build.cs").read_text(encoding="utf-8")
            self.assertNotIn("FJsonObject", source)
            self.assertNotIn('"Json"', build)
            self.assertIn('"UnrealMCPAssetCore"', build)

    def test_only_released_commands_are_registered(self):
        names = [tool["name"] for tool in TOOLS]
        self.assertEqual(names, [
            "capabilities", "editor_state", "operation_status", "operation_cancel",
            "asset_inspect", "asset_references", "asset_delete",
            "level_inspect", "level_open", "level_manage", "level_actor_edit", "level_save",
            "blueprint_action_catalog", "blueprint_graph_edit",
            "blueprint_block_replace",
            "blueprint_create", "blueprint_compile", "blueprint_save",
            "blueprint_component_edit", "blueprint_default_edit",
            "blueprint_member_edit", "widget_tree_edit",
            "gameplay_framework_edit", "game_data_inspect", "game_data_edit",
        ])
        bridge_source = native_host_source()
        for command in names:
            self.assertIn(f'TEXT("{command}")', bridge_source)

        catalog = (HOST_PRIVATE / "UnrealMCPCommandCatalog.cpp").read_text(encoding="utf-8")
        self.assertIn("Catalog.Freeze", catalog)
        self.assertIn("Descriptors.Sort", catalog)
        self.assertIn("Descriptor.Order", catalog)
        self.assertIn("Access::Internal", catalog)
        self.assertIn("Dispatch::RequestThread", catalog)

    def test_native_domains_have_explicit_load_and_ownership_boundaries(self):
        descriptor = json.loads(
            (ROOT / "plugin/UnrealMCP/UnrealMCP.uplugin").read_text(encoding="utf-8")
        )
        modules = {module["Name"]: module["LoadingPhase"] for module in descriptor["Modules"]}
        expected_domains = [
            "UnrealMCPAssetCore", "UnrealMCPBlueprint", "UnrealMCPUMG", "UnrealMCPContent",
        ]
        self.assertEqual(modules["UnrealMCP"], "PostEngineInit")
        self.assertEqual([modules[name] for name in expected_domains], ["None"] * 4)

        startup = (HOST_PRIVATE / "UnrealMCPModule.cpp").read_text(encoding="utf-8")
        positions = [startup.index(f'TEXT("{name}")') for name in expected_domains]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("LoadModuleWithFailureReason", startup)

        ownership = {
            ASSET_CORE_PRIVATE / "UnrealMCPAssetCoreModule.cpp": ["asset_inspect"],
            BLUEPRINT_PRIVATE / "UnrealMCPBlueprintModule.cpp": [
                "blueprint_action_catalog", "blueprint_graph_edit", "blueprint_block_replace",
                "blueprint_create", "blueprint_compile", "blueprint_save",
                "blueprint_component_edit", "blueprint_default_edit", "blueprint_member_edit",
                "gameplay_framework_edit",
            ],
            UMG_PRIVATE / "UnrealMCPUMGModule.cpp": ["widget_tree_edit"],
            CONTENT_PRIVATE / "UnrealMCPContentModule.cpp": [
                "asset_references", "asset_delete", "level_inspect", "level_open",
                "level_manage", "level_actor_edit", "level_save", "game_data_inspect",
                "game_data_edit",
            ],
        }
        host = (HOST_PRIVATE / "UnrealMCPCommandCatalog.cpp").read_text(encoding="utf-8")
        for path, commands in ownership.items():
            source = path.read_text(encoding="utf-8")
            for command in commands:
                self.assertIn(f'TEXT("{command}")', source)
                self.assertNotIn(f'Add(TEXT("{command}")', host)
        for forbidden in [
            "UnrealMCPBlueprintInspector.h", "UnrealMCPWidgetTreeService.h",
            "UnrealMCPGameDataService.h", "UnrealMCPLevelService.h",
        ]:
            self.assertNotIn(forbidden, host)

    def test_native_catalog_is_fixed_and_bridge_is_domain_neutral(self):
        root = HOST_PRIVATE
        bridge = (root / "UnrealMCPBridge.cpp").read_text(encoding="utf-8")
        catalog = (root / "UnrealMCPCommandCatalog.cpp").read_text(encoding="utf-8")
        self.assertNotIn("UnrealMCPBlueprintInspector.h", bridge)
        self.assertNotIn("UnrealMCPAssetInspectionService.h", bridge)
        self.assertNotIn("IsRetainedOperationCommand", bridge)
        self.assertNotIn("Features->SetBoolField", bridge)
        self.assertNotIn("Limits->SetNumberField", bridge)
        for rejection in [
            "Duplicate native command", "Conflicting native capability",
            "Native command catalog is frozen", "Runtime-provided commands and schemas are forbidden",
        ]:
            self.assertIn(rejection, catalog)
        self.assertIn("UnrealMCP.CommandCatalog.FixedCompositionAndRejection", (
            root / "Tests/UnrealMCPAutomationTestsCommandCatalog.cpp"
        ).read_text(encoding="utf-8"))

    def test_asset_inspection_coordinator_is_family_neutral(self):
        coordinator = (ASSET_CORE_PRIVATE / "UnrealMCPAssetInspectionService.cpp").read_text(encoding="utf-8")
        adapters = (BLUEPRINT_PRIVATE / "UnrealMCPAssetInspectionAdapters.cpp").read_text(encoding="utf-8")
        neutral_adapter = (ASSET_CORE_PRIVATE / "UnrealMCPNeutralAssetInspectionAdapter.cpp").read_text(encoding="utf-8")
        self.assertIn("AssetFamilyRegistry->Select", coordinator)
        self.assertIn("InspectionAdapter->Inspect", coordinator)
        for family_symbol in [
            "AGameMode", "AGameState", "APlayerController", "APlayerState",
            "UActorComponent", "BPTYPE_Interface", "AddFamilySemantics",
            "BuildCollectionSelection", "BuildSelectedGraph",
        ]:
            self.assertNotIn(family_symbol, coordinator)
        for adapter_symbol in [
            "FCoreBlueprintInspectionAdapter",
            "FBlueprintInterfaceInspectionAdapter", "FActorComponentBlueprintInspectionAdapter",
            "FGameplayBlueprintInspectionAdapter", "FBlueprintGraphInspectionAdapter",
            "FBlueprintCollectionInspectionAdapter", "FBlueprintSemanticPropertyAdapter",
        ]:
            self.assertIn(adapter_symbol, adapters)
        self.assertIn("FNeutralAssetInspectionAdapter", neutral_adapter)
        self.assertIn('TEXT("core_blueprint")', adapters)
        self.assertIn('TEXT("neutral_asset")', neutral_adapter)
        self.assertIn("UnrealMCP.AssetInspect.AdapterIsolation", (
            BLUEPRINT_PRIVATE / "Tests/UnrealMCPAutomationTestsAssetInspection.cpp"
        ).read_text(encoding="utf-8"))

    def test_asset_inspect_data_reuses_family_and_reflected_value_boundaries(self):
        content_module = (CONTENT_PRIVATE / "UnrealMCPContentModule.cpp").read_text(encoding="utf-8")
        adapters = (CONTENT_PRIVATE / "UnrealMCPDataInspectionAdapters.cpp").read_text(encoding="utf-8")
        structured = (BLUEPRINT_PRIVATE / "UnrealMCPStructuredDataInspection.cpp").read_text(encoding="utf-8")
        blueprint = (BLUEPRINT_PRIVATE / "UnrealMCPAssetInspectionAdapters.cpp").read_text(encoding="utf-8")
        native_test = (CONTENT_PRIVATE / "Tests/UnrealMCPAutomationTestsAssetInspectData.cpp").read_text(encoding="utf-8")
        self.assertIn("DataInspection::RegisterAdapters", content_module)
        self.assertIn('TEXT("asset_inspect_data")', content_module)
        for contract in [
            'TEXT("data_asset")', 'TEXT("data_table")', 'TEXT("properties")',
            'TEXT("asset_bundles")', 'TEXT("rows")', 'TEXT("columns")',
            "GameDataInspectionBuilder::Build", "StructuredDataInspection::InspectField",
        ]:
            self.assertIn(contract, adapters)
        self.assertIn("GameDataValueCodec::Encode", structured)
        self.assertIn("primary_data_asset_blueprint", blueprint)
        self.assertIn("UnrealMCP.AssetInspect.DataAssetsTablesSelectorsAndSnapshots", native_test)

    def test_asset_inspect_umg_composes_core_logic_with_bounded_umg_semantics(self):
        module = (UMG_PRIVATE / "UnrealMCPUMGModule.cpp").read_text(encoding="utf-8")
        adapter = (UMG_PRIVATE / "UnrealMCPUMGInspectionAdapter.cpp").read_text(encoding="utf-8")
        model = (UMG_PRIVATE / "UnrealMCPUMGInspectionModel.cpp").read_text(encoding="utf-8")
        structured = (BLUEPRINT_PRIVATE / "UnrealMCPStructuredDataInspection.cpp").read_text(encoding="utf-8")
        blueprint = (BLUEPRINT_PRIVATE / "UnrealMCPAssetInspectionAdapters.cpp").read_text(encoding="utf-8")
        native_test = (
            UMG_PRIVATE / "Tests/UnrealMCPAutomationTestsAssetInspectUMG.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("UMGInspection::RegisterAdapter", module)
        self.assertIn('TEXT("asset_inspect_umg")', module)
        for contract in [
            'TEXT("umg_widget")', 'TEXT("widget_tree")', 'TEXT("widgets")',
            'TEXT("named_slots")', 'TEXT("bindings")', 'TEXT("properties")',
            "bComposableInspectionOverlay", "UUserWidget::StaticClass()",
        ]:
            self.assertIn(contract, adapter)
        for limit in [
            "MaxWidgetTreeWidgets", "MaxWidgetTreeDepth",
            "MaxWidgetNamedSlots", "MaxWidgetBindings",
        ]:
            self.assertIn(limit, adapter + model)
        self.assertIn("BuildSelectedPropertyPage", structured)
        self.assertIn('Result.Type = TEXT("widget_blueprint")', blueprint)
        self.assertNotIn("CommonUI", adapter + model)
        self.assertNotIn("MovieScene", adapter + model)
        self.assertIn(
            "UnrealMCP.AssetInspect.UMGHierarchyLayoutBindingsAndExclusions", native_test
        )

    def test_asset_references_is_published_bounded_and_covered(self):
        bridge = native_host_source()
        source_dir = CONTENT_PRIVATE
        components = {
            name: (source_dir / f"UnrealMCPAssetReference{name}.cpp").read_text(encoding="utf-8")
            for name in [
                "Service", "TargetResolver", "RegistryScanner", "LiveScanner",
                "SnapshotBuilder", "CursorStore",
            ]
        }
        native_test = (CONTENT_PRIVATE / "Tests/UnrealMCPAutomationTestsAssetReferences.cpp").read_text(encoding="utf-8")
        for feature in ["asset_reference_discovery", "asset_reference_live_memory"]:
            self.assertIn(f'TEXT("{feature}")', bridge)
        self.assertIn("MaxAssetReferenceRegistryCandidates", components["RegistryScanner"])
        self.assertIn("MaxAssetReferenceLiveObjects", components["LiveScanner"])
        self.assertIn("MaxAssetReferenceRecords", components["RegistryScanner"] + components["LiveScanner"])
        self.assertIn("MaxAssetReferenceRetainedCursors", components["CursorStore"])
        for evidence in ["serialized", "management", "searchable_name"]:
            self.assertIn(f'TEXT("{evidence}")', components["RegistryScanner"] + components["SnapshotBuilder"])
        self.assertIn('TEXT("live_memory")', components["LiveScanner"] + components["SnapshotBuilder"])
        for collaborator in ["TargetResolver", "RegistryScanner", "LiveScanner", "SnapshotBuilder", "CursorStore"]:
            self.assertIn(f"FUnrealMCPAssetReference{collaborator}", "".join(components.values()))
        self.assertIn("UnrealMCP.AssetReferences.RegistryLiveMemoryAndCursors", native_test)

    def test_asset_delete_is_ledger_backed_conservative_and_covered(self):
        bridge = native_host_source()
        service = (CONTENT_PRIVATE / "UnrealMCPAssetDeletionService.cpp").read_text(encoding="utf-8")
        native_test = (CONTENT_PRIVATE / "Tests/UnrealMCPAutomationTestsAssetDelete.cpp").read_text(encoding="utf-8")
        self.assertIn('TEXT("asset_delete")', bridge)
        self.assertIn('TEXT("asset_delete")', bridge)
        for safety in [
            "IsPlayingSessionInEditor()", "IsSimulatingInEditor()", "IsSavingPackage()",
            "IsGarbageCollecting()", "IsTransactionActive()", "IsAsyncLoading()",
            "GatherObjectReferencersForDeletion", "DeleteSingleObject", "CleanupAfterSuccessfulDelete",
        ]:
            self.assertIn(safety, service)
        self.assertNotIn("DeleteObjectsUnchecked", service)
        self.assertNotIn("ForceDeleteObjects", service)
        self.assertIn("UnrealMCP.AssetDelete.PreflightPersistenceAndReferences", native_test)

    def test_level_open_is_published_and_covered(self):
        bridge = native_host_source()
        service = (CONTENT_PRIVATE / "UnrealMCPLevelService.cpp").read_text(encoding="utf-8")
        native_test = (CONTENT_PRIVATE / "Tests/UnrealMCPAutomationTestsLevelOpen.cpp").read_text(encoding="utf-8")
        for feature in ["level_discovery", "level_open", "level_snapshots"]:
            self.assertIn(f'TEXT("{feature}")', bridge)
        for safety in ["IsPlayingSessionInEditor()", "IsSimulatingInEditor()", "IsSavingPackage()",
                       "IsGarbageCollecting()", "IsTransactionActive()", "IsAsyncLoading()"]:
            self.assertIn(safety, service)
        self.assertIn("FEditorFileUtils::LoadMap", service)
        self.assertIn("UnrealMCP.LevelOpen.DiscoverySnapshotsAndSafety", native_test)

    def test_level_management_is_ledger_backed_bounded_and_covered(self):
        root = CONTENT_PRIVATE
        bridge = native_host_source(root)
        service = (root / "UnrealMCPLevelManagementService.cpp").read_text(encoding="utf-8")
        deletion = (root / "UnrealMCPAssetDeletionService.cpp").read_text(encoding="utf-8")
        native_test = (root / "Tests/UnrealMCPAutomationTestsLevelManagement.cpp").read_text(encoding="utf-8")
        self.assertIn('TEXT("level_manage")', bridge)
        for feature in [
            "level_management", "level_blank_creation", "level_template_creation",
            "level_world_settings", "level_map_deletion",
        ]:
            self.assertIn(f'TEXT("{feature}")', bridge)
        for safety in [
            "IsPlayingSessionInEditor()", "IsSimulatingInEditor()", "IsSavingPackage()",
            "IsGarbageCollecting()", "IsTransactionActive()", "IsAsyncLoading()",
        ]:
            self.assertIn(safety, service)
        self.assertIn("MaxLevelSetupProperties", service + bridge)
        self.assertIn("MaxLevelOwnedPackages", deletion + bridge)
        self.assertIn("PropertyCodec::Set", service)
        self.assertIn("UPackageTools::UnloadPackages", service)
        self.assertIn("ObjectTools::DeleteAssets", deletion)
        self.assertNotIn("DeleteObjectsUnchecked", deletion)
        self.assertIn("UnrealMCP.LevelManagement.CreateConfigurePersistAndDelete", native_test)

    def test_level_inspect_is_bounded_read_only_and_covered(self):
        bridge = native_host_source()
        service = (CONTENT_PRIVATE / "UnrealMCPLevelActorInspector.cpp").read_text(encoding="utf-8")
        native_test = (CONTENT_PRIVATE / "Tests/UnrealMCPAutomationTestsLevelInspect.cpp").read_text(encoding="utf-8")
        for feature in [
            "level_actor_inspection", "level_world_partition_descriptors",
            "level_targeted_actor_loading", "level_instance_properties",
        ]:
            self.assertIn(f'TEXT("{feature}")', bridge)
        for limit in [
            "MaxLevelActorScan", "MaxLevelActorRecords", "MaxLevelComponents",
            "MaxLevelActorTags", "MaxLevelDataLayers", "MaxLevelTargetedLoads",
        ]:
            self.assertIn(limit, service + bridge)
        self.assertIn("FWorldPartitionHelpers::ForEachActorDescInstance", service)
        self.assertIn("FWorldPartitionReference", service)
        self.assertNotIn("FScopedTransaction", service)
        self.assertIn("UnrealMCP.LevelInspect.ActorsComponentsPropertiesAndSafety", native_test)

    def test_level_edit_is_ledger_backed_transactional_and_covered(self):
        root = CONTENT_PRIVATE
        bridge = native_host_source(root)
        service = (root / "UnrealMCPLevelActorEditingService.cpp").read_text(encoding="utf-8")
        native_test = (root / "Tests/UnrealMCPAutomationTestsLevelEdit.cpp").read_text(encoding="utf-8")
        production = (ROOT / "scripts/headless_integration/level_editing.py").read_text(encoding="utf-8")
        for command in ["level_actor_edit", "level_save"]:
            self.assertIn(f'TEXT("{command}")', bridge)
        for feature in [
            "level_actor_editing", "level_actor_transactions",
            "level_package_save_verification",
        ]:
            self.assertIn(f'TEXT("{feature}")', bridge)
        for limit in ["MaxLevelEditOperations", "MaxLevelEditActors", "MaxLevelSavePackages"]:
            self.assertIn(limit, service + bridge)
        for safety in [
            "IsPlayingSessionInEditor()", "IsSimulatingInEditor()", "IsSavingPackage()",
            "IsGarbageCollecting()", "IsTransactionActive()", "IsAsyncLoading()",
        ]:
            self.assertIn(safety, service)
        for contract in [
            "FScopedTransaction", "UndoTransaction", "FWorldPartitionReference",
            "PropertyCodec::Set", "SavePackages", "FEditorFileUtils::LoadMap",
            'TEXT("mutation_scope_denied")', "FPaths::ProjectContentDir()",
        ]:
            self.assertIn(contract, service)
        self.assertIn("UnrealMCP.LevelEdit.TransactionalActorBatchAndPackageSave", native_test)
        for acceptance in [
            "author_level_edit_scenario", "verify_restarted_level_edit",
            'send_without_reading(layout, "level_actor_edit"',
            'send_without_reading(layout, "level_save"',
        ]:
            self.assertIn(acceptance, production)

    def test_phase_sixteen_multiplayer_policy_is_published_and_covered(self):
        policy = (BLUEPRINT_PRIVATE / "UnrealMCPBlueprintFamilyPolicy.cpp").read_text(encoding="utf-8")
        bridge = native_host_source()
        phase_fourteen_test = (BLUEPRINT_PRIVATE / "Tests/UnrealMCPAutomationTestsPhase14.cpp").read_text(encoding="utf-8")
        phase_fifteen_test = (BLUEPRINT_PRIVATE / "Tests/UnrealMCPAutomationTestsPhase15.cpp").read_text(encoding="utf-8")
        phase_sixteen_test = (BLUEPRINT_PRIVATE / "Tests/UnrealMCPAutomationTestsPhase16.cpp").read_text(encoding="utf-8")
        for family in ["actor", "game_mode_base", "game_mode", "game_state_base", "game_state", "game_instance"]:
            self.assertIn(f'TEXT("{family}")', policy)
        for family in ["game_mode_base", "game_mode", "game_state_base", "game_state"]:
            self.assertIn(f'TEXT("{family}")', phase_fourteen_test)
        self.assertIn('TEXT("game_instance")', phase_fifteen_test)
        self.assertIn('GetBoolField(TEXT("components"))', phase_fifteen_test)
        self.assertIn('TEXT("invalid_component")', phase_fifteen_test)
        self.assertIn('TEXT("parent_change"), false', policy)
        self.assertIn('TEXT("project_settings_assignment")', policy)
        self.assertIn('TEXT("server")', phase_sixteen_test)
        self.assertIn('TEXT("multicast")', phase_sixteen_test)
        self.assertIn('TEXT("gameplay_framework_edit")', bridge)
        self.assertIn('TEXT("blueprint_families")', bridge)
        self.assertIn('TEXT("game_instance_family")', bridge)

    def test_phase_seventeen_game_data_is_published_and_covered(self):
        bridge = native_host_source()
        service = (CONTENT_PRIVATE / "UnrealMCPGameDataOperationHandlers.cpp").read_text(encoding="utf-8")
        test = (CONTENT_PRIVATE / "Tests/UnrealMCPAutomationTestsPhase17.cpp").read_text(encoding="utf-8")
        for command in ["game_data_inspect", "game_data_edit"]:
            self.assertIn(f'TEXT("{command}")', bridge)
        for feature in ["user_defined_struct_authoring", "typed_data_tables", "game_data_batch_editing"]:
            self.assertIn(f'TEXT("{feature}")', bridge)
        for operation in ["add_member", "reorder_member", "add_row", "replace_row", "rename_row", "remove_row", "batch"]:
            self.assertIn(f'TEXT("{operation}")', service)
        self.assertIn('UnrealMCP.Phase17.GameDataAuthoring', test)

    def test_game_data_and_graph_editor_have_focused_native_boundaries(self):
        expected_units = {
            CONTENT_PRIVATE / "UnrealMCPGameDataRequestValidation.cpp": "ValidateEditShape",
            CONTENT_PRIVATE / "UnrealMCPGameDataOperationHandlers.cpp": "FUnrealMCPGameDataService::Edit",
            CONTENT_PRIVATE / "UnrealMCPGameDataInspectionBuilder.cpp": "GameDataInspectionBuilder",
            BLUEPRINT_PRIVATE / "UnrealMCPBlueprintGraphRequestValidation.cpp": "BlueprintGraphRequestValidation",
            BLUEPRINT_PRIVATE / "UnrealMCPBlueprintGraphOperationHandlers.cpp": "FUnrealMCPBlueprintGraphEditor::Execute",
            BLUEPRINT_PRIVATE / "UnrealMCPBlueprintGraphPinOperationHandler.cpp": "BlueprintGraphPinOperationHandler",
            BLUEPRINT_PRIVATE / "UnrealMCPBlueprintGraphResultBuilder.cpp": "BlueprintGraphResultBuilder",
        }
        for path, owner in expected_units.items():
            source = path.read_text(encoding="utf-8")
            self.assertIn(owner, source)
            self.assertLessEqual(len(source.splitlines()), 600)

    def test_widget_tree_is_family_scoped_bounded_and_covered(self):
        policy = (BLUEPRINT_PRIVATE / "UnrealMCPBlueprintFamilyPolicy.cpp").read_text(encoding="utf-8")
        service = (UMG_PRIVATE / "UnrealMCPWidgetTreeService.cpp").read_text(encoding="utf-8")
        inspector = (BLUEPRINT_PRIVATE / "UnrealMCPWidgetTreeInspector.h").read_text(encoding="utf-8")
        bridge = native_host_source()
        native_test = (
            UMG_PRIVATE / "Tests/UnrealMCPAutomationTestsWidgetTree.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn('TEXT("widget")', policy)
        self.assertIn("UUserWidget::StaticClass()", policy)
        self.assertIn('TEXT("widget_tree_edit")', bridge)
        for feature in [
            "widget_blueprint_family", "widget_tree_authoring",
            "umg_layout_authoring", "umg_style_authoring",
            "umg_property_bindings", "umg_designer_events",
        ]:
            self.assertIn(f'TEXT("{feature}")', bridge)
        for limit in [
            "MaxWidgetTreeWidgets", "MaxWidgetTreeDepth", "MaxWidgetNamedSlots",
            "MaxWidgetDefaultsPerWidget", "MaxWidgetChangedDefaults",
            "MaxWidgetBindings",
        ]:
            self.assertIn(limit, bridge + inspector)
        for operation in [
            "set_root", "add", "remove", "rename", "reparent",
            "set_variable", "set_property",
            "set_slot", "set_style", "bind_property", "unbind_property",
            "bind_event", "unbind_event",
        ]:
            self.assertIn(f'TEXT("{operation}")', service)
        self.assertIn(
            "UnrealMCP.WidgetTree.FamilyInspectionMutationAndPersistence",
            native_test,
        )

    def test_umg_authoring_has_focused_native_components(self):
        expected = {
            "UnrealMCPWidgetAuthoringSupport.cpp": "ApplyProperty",
            "UnrealMCPWidgetLayoutService.cpp": "FUnrealMCPWidgetLayoutService::Execute",
            "UnrealMCPWidgetStyleService.cpp": "FUnrealMCPWidgetStyleService::Execute",
            "UnrealMCPWidgetBindingService.cpp": "FUnrealMCPWidgetBindingService::Execute",
        }
        for filename, owner in expected.items():
            source = (UMG_PRIVATE / filename).read_text(encoding="utf-8")
            self.assertIn(owner, source)
            self.assertLessEqual(len(source.splitlines()), 600)
        inspector = (BLUEPRINT_PRIVATE / "UnrealMCPWidgetTreeInspector.h").read_text(encoding="utf-8")
        self.assertIn('TEXT("widget_bindings")', inspector)
        self.assertIn("WidgetInspection::FingerprintLayout", inspector)
        native_test = (
            UMG_PRIVATE / "Tests/UnrealMCPAutomationTestsUMGAuthoring.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "UnrealMCP.UMGAuthoring.LayoutStyleBindingsAndEvents",
            native_test,
        )

    def test_editor_lifecycle_is_independently_configured_and_natively_guarded(self):
        self.assertNotIn("editor_lifecycle", [tool["name"] for tool in TOOLS])
        self.assertEqual([tool["name"] for tool in TOOLS_WITH_LIFECYCLE][-1], "editor_lifecycle")
        bridge = native_host_source()
        protocol = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/UnrealMCPProtocol.cpp").read_text(encoding="utf-8")
        native_test = (ROOT / "plugin/UnrealMCP/Source/UnrealMCP/Private/Tests/UnrealMCPAutomationTestsLifecycle.cpp").read_text(encoding="utf-8")
        self.assertIn('TEXT("editor_shutdown")', bridge)
        self.assertIn('TEXT("editor_shutdown")', protocol)
        self.assertIn("IsTransactionActive()", bridge)
        self.assertIn("GetNumRemainingAssets()", bridge)
        self.assertIn("IsPlayingSessionInEditor()", bridge)
        self.assertIn("IsSavingPackage()", bridge)
        self.assertIn("RequestExit(false)", bridge)
        self.assertNotIn("RequestExit(true)", bridge)
        self.assertIn("rejects forced termination", native_test)

    def test_every_docs_directory_has_an_index(self):
        for directory in [path for path in (ROOT / "docs").rglob("*") if path.is_dir()]:
            with self.subTest(directory=directory):
                self.assertTrue((directory / "index.md").is_file())
