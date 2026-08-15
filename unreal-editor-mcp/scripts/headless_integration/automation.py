"""Unreal Automation commands, expected cases, timeouts, logs, and fixtures."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

from .process_lifecycle import stop_editor

ROOT = Path(__file__).resolve().parents[2]

def run_automation(executable: Path, project: Path, environment: dict[str, str], test_filter: str = "UnrealMCP") -> int:
    all_expected = (
        "CompatibilityBranch",
        "ErrorEnvelope",
        "GameThreadDispatch",
        "InvalidTokenFailsClosed",
        "ProtocolBounds",
        "RouteGuards",
        "TokenPersistence",
        "CursorGuards",
        "InspectionContracts",
        "LiveFixture",
        "CreationContracts",
        "FailureCleanup",
        "CreationLiveFixture",
        "ComponentAndDefaultEdits",
        "CodecValidation",
        "BlueprintDefaultsAndComponents",
        "OperationLedger",
        "PropertyCodec",
        "K2TypeCodec",
        "MemberVariables",
        "FunctionsAndLocals",
        "MacrosAndCustomEvents",
        "ActionCatalog",
        "ExpandedActionCatalog",
        "GraphNodeLifecycle",
        "PinDefaultsAndDirectConnections",
        "WildcardsConversionsAndAtomicGraphEditing",
        "GameModeAndGameStateFamilies",
        "GameInstanceFamily",
        "MultiplayerAuthoring",
        "FrameworkAssignment",
        "GameDataAuthoring",
        "FixedCompositionAndRejection",
        "CapabilitiesAndRemovedInspectionRoute",
        "AdmissionAndLifecycleFailures",
        "BlueprintFamilyInspectionIntegration",
        "DiscoverySnapshotsAndSafety",
        "ActorsComponentsPropertiesAndSafety",
        "CreateConfigurePersistAndDelete",
        "TransactionalActorBatchAndPackageSave",
        "PreflightPersistenceAndReferences",
        "RegistryLiveMemoryAndCursors",
        "BoundedBuildersAndSyntheticAdapter",
        "RegistrySelectionCapabilitiesAndFreeze",
        "FamilyInspectionMutationAndPersistence",
        "LayoutStyleBindingsAndEvents",
        "AdapterIsolation",
        "CoreFamiliesSelectorsPagingAndLimits",
        "DataAssetsTablesSelectorsAndSnapshots",
        "UMGHierarchyLayoutBindingsAndExclusions",
        "PreflightTransactionPreservation",
        "LogicUnitsAndExternalLinks",
        "DeterministicChangedNodes",
        "PreservationAcrossReadonlyFlows",
        "RoundTrip",
        "InvalidInput",
        "EnvelopeFixtures",
        "AbilityBlueprintInspection",
        "GameplayEffectInspection",
        "GameplayEffectLiveFixture",
        "SupportingAssetInspection",
        "WidgetBlueprintInspection",
        "WidgetBlueprintLiveFixture",
        "Protocol",
    )
    if test_filter == "UnrealMCP":
        expected = all_expected
    elif test_filter == "UnrealMCP.Phase4":
        expected = tuple(name for name in all_expected if name in {
            "ComponentAndDefaultEdits", "OperationLedger", "PropertyCodec",
        })
    elif test_filter == "UnrealMCP.Phase5":
        expected = tuple(name for name in all_expected if name in {"K2TypeCodec", "MemberVariables"})
    elif test_filter == "UnrealMCP.Phase6":
        expected = tuple(name for name in all_expected if name == "FunctionsAndLocals")
    elif test_filter == "UnrealMCP.Phase7":
        expected = tuple(name for name in all_expected if name == "MacrosAndCustomEvents")
    elif test_filter == "UnrealMCP.Phase8":
        expected = tuple(name for name in all_expected if name == "ActionCatalog")
    elif test_filter == "UnrealMCP.Phase10":
        expected = tuple(name for name in all_expected if name == "ExpandedActionCatalog")
    elif test_filter == "UnrealMCP.Phase11":
        expected = tuple(name for name in all_expected if name == "GraphNodeLifecycle")
    elif test_filter == "UnrealMCP.Phase12":
        expected = tuple(name for name in all_expected if name == "PinDefaultsAndDirectConnections")
    elif test_filter == "UnrealMCP.Phase13":
        expected = tuple(name for name in all_expected if name == "WildcardsConversionsAndAtomicGraphEditing")
    elif test_filter == "UnrealMCP.Phase14":
        expected = tuple(name for name in all_expected if name == "GameModeAndGameStateFamilies")
    elif test_filter == "UnrealMCP.Phase15":
        expected = tuple(name for name in all_expected if name == "GameInstanceFamily")
    elif test_filter == "UnrealMCP.Phase16":
        expected = tuple(name for name in all_expected if name in {"MultiplayerAuthoring", "FrameworkAssignment"})
    elif test_filter == "UnrealMCP.Phase17":
        expected = tuple(name for name in all_expected if name == "GameDataAuthoring")
    elif test_filter == "UnrealMCP.GameplayTagProperties":
        expected = tuple(name for name in all_expected if name in {
            "CodecValidation", "BlueprintDefaultsAndComponents",
        })
    elif test_filter == "UnrealMCP.AssetReferences":
        expected = tuple(name for name in all_expected if name == "RegistryLiveMemoryAndCursors")
    elif test_filter == "UnrealMCP.AssetInspect":
        expected = tuple(name for name in all_expected if name in {
            "AdapterIsolation", "CoreFamiliesSelectorsPagingAndLimits",
            "DataAssetsTablesSelectorsAndSnapshots",
            "UMGHierarchyLayoutBindingsAndExclusions",
        })
    elif test_filter == "UnrealMCP.LevelManagement":
        expected = tuple(name for name in all_expected if name == "CreateConfigurePersistAndDelete")
    elif test_filter == "UnrealMCP.CommonUI":
        expected = tuple(name for name in all_expected if name in {
            "WidgetBlueprintInspection", "WidgetBlueprintLiveFixture",
        })
    else:
        leaf = test_filter.rsplit(".", 1)[-1]
        expected = (leaf,) if leaf in all_expected else ()
    command = [
        str(executable), str(project), "-unattended", "-nop4", "-nosplash", "-nullrhi",
        "-stdout", "-FullStdOutLogOutput", "-nocrashreports", "-NoAssetRegistryCache",
        "-DDC-ForceMemoryCache",
        f"-ExecCmds=Automation RunTests {test_filter};Quit",
        "-TestExit=Automation Test Queue Empty",
    ]
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            return_code = process.wait(timeout=180.0)
        except subprocess.TimeoutExpired:
            stop_editor(process)
            raise RuntimeError("Unreal Automation Tests exceeded the three-minute deadline")
        log.seek(0)
        output = log.read().decode("utf-8", errors="replace")
    missing = [name for name in expected if f"Result={{Success}} Name={{{name}}}" not in output]
    if return_code != 0 or "TEST COMPLETE. EXIT CODE: 0" not in output or missing:
        sys.stderr.write(output[-32_000:])
        if missing:
            raise RuntimeError(f"Unreal Automation Tests did not pass: {', '.join(missing)}")
        raise RuntimeError(f"Unreal Automation Tests exited with status {return_code}")
    print(f"Unreal Automation Tests passed: {len(expected)} native cases")
    return 0


def prepare_phase_two_fixture(executable: Path, project: Path, environment: dict[str, str]) -> str:
    command = [
        str(executable), str(project), "-unattended", "-nop4", "-nosplash", "-nullrhi",
        "-stdout", "-FullStdOutLogOutput", "-nocrashreports", "-NoAssetRegistryCache",
        "-DDC-ForceMemoryCache",
        "-ExecCmds=Automation RunTests UnrealMCP.Phase2.LiveFixture;Quit",
        "-TestExit=Automation Test Queue Empty",
    ]
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            return_code = process.wait(timeout=180.0)
        except subprocess.TimeoutExpired:
            stop_editor(process)
            raise RuntimeError("Phase 2 fixture preparation exceeded the three-minute deadline")
        log.seek(0)
        output = log.read().decode("utf-8", errors="replace")
    marker = "UNREAL_MCP_PHASE2_SNAPSHOT="
    snapshots = [line.split(marker, 1)[1].split()[0] for line in output.splitlines() if marker in line]
    if return_code != 0 or "Result={Success} Name={LiveFixture}" not in output or not snapshots:
        sys.stderr.write(output[-32_000:])
        raise RuntimeError("Phase 2 saved fixture preparation failed")
    return snapshots[-1]


def prepare_gas_effect_fixture(executable: Path, project: Path, environment: dict[str, str]) -> str:
    command = [
        str(executable), str(project), "-unattended", "-nop4", "-nosplash", "-nullrhi",
        "-stdout", "-FullStdOutLogOutput", "-nocrashreports", "-NoAssetRegistryCache",
        "-DDC-ForceMemoryCache",
        "-ExecCmds=Automation RunTests UnrealMCP.GAS.GameplayEffectLiveFixture;Quit",
        "-TestExit=Automation Test Queue Empty",
    ]
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            return_code = process.wait(timeout=180.0)
        except subprocess.TimeoutExpired:
            stop_editor(process)
            raise RuntimeError("Gameplay Effect fixture preparation exceeded the three-minute deadline")
        log.seek(0)
        output = log.read().decode("utf-8", errors="replace")
    marker = "UNREAL_MCP_GAS_EFFECT_FIXTURE="
    fixtures = [line.split(marker, 1)[1].split()[0] for line in output.splitlines() if marker in line]
    if return_code != 0 or "Result={Success} Name={GameplayEffectLiveFixture}" not in output or not fixtures:
        sys.stderr.write(output[-32_000:])
        raise RuntimeError("Gameplay Effect saved fixture preparation failed")
    return fixtures[-1]


def prepare_commonui_widget_fixture(
    executable: Path, project: Path, environment: dict[str, str],
) -> str:
    command = [
        str(executable), str(project), "-unattended", "-nop4", "-nosplash", "-nullrhi",
        "-stdout", "-FullStdOutLogOutput", "-nocrashreports", "-NoAssetRegistryCache",
        "-DDC-ForceMemoryCache",
        "-ExecCmds=Automation RunTests UnrealMCP.CommonUI.WidgetBlueprintLiveFixture;Quit",
        "-TestExit=Automation Test Queue Empty",
    ]
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen(command, cwd=ROOT, env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            return_code = process.wait(timeout=180.0)
        except subprocess.TimeoutExpired:
            stop_editor(process)
            raise RuntimeError("CommonUI Widget fixture preparation exceeded the three-minute deadline")
        log.seek(0)
        output = log.read().decode("utf-8", errors="replace")
    marker = "UNREAL_MCP_COMMONUI_FIXTURE="
    fixtures = [line.split(marker, 1)[1].split()[0] for line in output.splitlines() if marker in line]
    if return_code != 0 \
            or "Result={Success} Name={WidgetBlueprintLiveFixture}" not in output \
            or not fixtures:
        sys.stderr.write(output[-32_000:])
        raise RuntimeError("CommonUI Widget saved fixture preparation failed")
    return fixtures[-1]


