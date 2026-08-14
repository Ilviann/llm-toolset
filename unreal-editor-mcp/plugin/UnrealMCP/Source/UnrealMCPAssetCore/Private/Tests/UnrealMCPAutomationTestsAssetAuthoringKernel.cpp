#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Editor.h"
#include "Engine/DataTable.h"
#include "Misc/SecureHash.h"
#include "UnrealMCPAssetAuthoringKernel.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
FString SnapshotFor(const FString& Value)
{
    FTCHARToUTF8 Encoded(*Value);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

void RetireFixture(UPackage* Package, UObject* Asset)
{
    if (Asset != nullptr)
    {
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->Rename(
            nullptr, GetTransientPackage(),
            REN_DontCreateRedirectors | REN_NonTransactional);
        Asset->MarkAsGarbage();
    }
    if (Package != nullptr)
    {
        Package->SetDirtyFlag(false);
        Package->Rename(
            *(TEXT("/Temp/UnrealMCPKernelTest_")
                + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
            nullptr,
            REN_DontCreateRedirectors | REN_NonTransactional);
        Package->MarkAsGarbage();
    }
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPAssetAuthoringKernelTest,
    "UnrealMCP.AssetAuthoring.KernelLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPAssetAuthoringKernelTest::RunTest(const FString& Parameters)
{
    const FString Base = TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString PackageName = Base + TEXT("/DT_KernelEdit");
    const FString ObjectPath =
        FUnrealMCPAssetAuthoringKernel::ObjectPathForPackage(PackageName);
    UPackage* Package = CreatePackage(*PackageName);
    UDataTable* Table = NewObject<UDataTable>(
        Package,
        FName(TEXT("DT_KernelEdit")),
        RF_Public | RF_Standalone | RF_Transactional);
    Table->RowStruct = FTableRowBase::StaticStruct();
    Table->ImportKeyField = TEXT("Before");

    FUnrealMCPAssetCreationHooks CollisionHooks;
    CollisionHooks.Create = [](UPackage*, UObject*&, FUnrealMCPError&) { return true; };
    CollisionHooks.Persist = [](UObject*, FUnrealMCPError&) { return true; };
    CollisionHooks.ReadBack = [](UObject*, FString& Snapshot, FUnrealMCPError&)
    {
        Snapshot = SnapshotFor(TEXT("collision"));
        return true;
    };
    FUnrealMCPAssetCreationResult CreationResult;
    FUnrealMCPError Error;
    TestFalse(
        TEXT("creation collision rejects before adapter execution"),
        FUnrealMCPAssetAuthoringKernel::ExecuteCreation(
            {FString(), PackageName, ObjectPath},
            CollisionHooks,
            CreationResult,
            Error));
    TestEqual(TEXT("collision error is stable"), Error.Code, FString(TEXT("already_exists")));

    const FString FailedPackageName = Base + TEXT("/DT_FailedCreation");
    const FString FailedObjectPath =
        FUnrealMCPAssetAuthoringKernel::ObjectPathForPackage(FailedPackageName);
    FUnrealMCPAssetCreationHooks FailedHooks;
    FailedHooks.Create = [](UPackage* Outer, UObject*& OutAsset, FUnrealMCPError&)
    {
        OutAsset = NewObject<UDataTable>(
            Outer, FName(TEXT("DT_FailedCreation")),
            RF_Public | RF_Standalone | RF_Transactional);
        CastChecked<UDataTable>(OutAsset)->RowStruct = FTableRowBase::StaticStruct();
        return OutAsset != nullptr;
    };
    FailedHooks.Finalize = [](UObject*, FUnrealMCPError& OutError)
    {
        OutError = {TEXT("compile_failed"), TEXT("Synthetic finalization failure")};
        return false;
    };
    FailedHooks.Persist = [](UObject*, FUnrealMCPError&) { return true; };
    FailedHooks.ReadBack = [](UObject*, FString&, FUnrealMCPError&) { return false; };
    Error = {};
    TestFalse(
        TEXT("failed creation rejects"),
        FUnrealMCPAssetAuthoringKernel::ExecuteCreation(
            {FString(), FailedPackageName, FailedObjectPath},
            FailedHooks,
            CreationResult,
            Error));
    TestEqual(TEXT("finalization error is retained"), Error.Code, FString(TEXT("compile_failed")));
    TestNull(
        TEXT("failed creation removes the exact package identity"),
        FindPackage(nullptr, *FailedPackageName));

    int32 MutationCount = 0;
    FUnrealMCPAssetEditHooks Hooks;
    Hooks.ReadBack = [](UObject* Asset, FString& Snapshot, FUnrealMCPError&)
    {
        Snapshot = SnapshotFor(CastChecked<UDataTable>(Asset)->ImportKeyField);
        return true;
    };
    Hooks.Mutate = [&MutationCount](UObject* Asset, FUnrealMCPError&)
    {
        ++MutationCount;
        CastChecked<UDataTable>(Asset)->ImportKeyField = TEXT("Accepted");
        return true;
    };
    FUnrealMCPAssetEditRequest Request{
        FString(), ObjectPath, SnapshotFor(TEXT("stale")),
        TEXT("Unreal MCP kernel test"), Table, false, true};
    FUnrealMCPAssetEditResult EditResult;
    Error = {};
    TestFalse(
        TEXT("stale edit rejects"),
        FUnrealMCPAssetAuthoringKernel::ExecuteEdit(
            Request, Hooks, EditResult, Error));
    TestEqual(TEXT("stale error is stable"), Error.Code, FString(TEXT("stale_precondition")));
    TestEqual(TEXT("stale edit never reaches semantics"), MutationCount, 0);

    Request.ExpectedSnapshot = SnapshotFor(TEXT("Before"));
    Hooks.Mutate = [](UObject*, FUnrealMCPError&) { return false; };
    Error = {};
    TestFalse(
        TEXT("no-op rejects"),
        FUnrealMCPAssetAuthoringKernel::ExecuteEdit(
            Request, Hooks, EditResult, Error));
    TestEqual(TEXT("no-op error is stable"), Error.Code, FString(TEXT("no_change")));
    TestEqual(TEXT("no-op restores the exact value"), Table->ImportKeyField, FString(TEXT("Before")));

    Hooks.Mutate = [](UObject* Asset, FUnrealMCPError&)
    {
        CastChecked<UDataTable>(Asset)->ImportKeyField = TEXT("Rejected");
        return true;
    };
    Hooks.ReadBack = [](UObject* Asset, FString& Snapshot, FUnrealMCPError&)
    {
        const FString Value = CastChecked<UDataTable>(Asset)->ImportKeyField;
        if (Value == TEXT("Rejected"))
        {
            return false;
        }
        Snapshot = SnapshotFor(Value);
        return true;
    };
    Error = {};
    TestFalse(
        TEXT("failed postcondition rejects"),
        FUnrealMCPAssetAuthoringKernel::ExecuteEdit(
            Request, Hooks, EditResult, Error));
    TestEqual(TEXT("failed postcondition retains its error"), Error.Code, FString(TEXT("internal_error")));
    TestEqual(TEXT("failed postcondition rolls back exactly"), Table->ImportKeyField, FString(TEXT("Before")));

    Hooks.ReadBack = [](UObject* Asset, FString& Snapshot, FUnrealMCPError&)
    {
        Snapshot = SnapshotFor(CastChecked<UDataTable>(Asset)->ImportKeyField);
        return true;
    };
    Hooks.Mutate = [](UObject* Asset, FUnrealMCPError&)
    {
        CastChecked<UDataTable>(Asset)->ImportKeyField = TEXT("Accepted");
        return true;
    };
    Error = {};
    TestTrue(
        TEXT("admitted edit commits"),
        FUnrealMCPAssetAuthoringKernel::ExecuteEdit(
            Request, Hooks, EditResult, Error));
    TestEqual(TEXT("committed edit returns exact read-back"), EditResult.SnapshotId, SnapshotFor(TEXT("Accepted")));
    TestEqual(TEXT("committed edit changes the live value"), Table->ImportKeyField, FString(TEXT("Accepted")));
    TestTrue(TEXT("committed edit is undoable"), GEditor != nullptr && GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores the prior value"), Table->ImportKeyField, FString(TEXT("Before")));
    TestTrue(TEXT("committed edit is redoable"), GEditor != nullptr && GEditor->RedoTransaction());
    TestEqual(TEXT("Redo restores the accepted value"), Table->ImportKeyField, FString(TEXT("Accepted")));
    TestTrue(TEXT("fixture returns to its original state"), GEditor != nullptr && GEditor->UndoTransaction());

    RetireFixture(Package, Table);
    return true;
}

#endif
