#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPReadonlyPreservationTest,
    "UnrealMCP.Readonly.PreservationAcrossReadonlyFlows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPReadonlyPreservationTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName =
        TEXT("/Game/UnrealMCPReadonlyTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits)
        + TEXT("/BP_Preservation");
    UBlueprint* Blueprint =
        CreateBlueprintFixture(PackageName, AActor::StaticClass(), true);
    if (!TestNotNull(TEXT("readonly preservation fixture is created"), Blueprint))
    {
        return false;
    }
    if (!TestTrue(TEXT("readonly preservation fixture saves cleanly"), SaveBlueprintFixture(Blueprint)))
    {
        return false;
    }
    UPackage* Package = Blueprint->GetOutermost();
    const bool bDirtyBefore = Package->IsDirty();
    const int32 UndoQueueBefore =
        GEditor != nullptr && GEditor->Trans != nullptr
        ? GEditor->Trans->GetQueueLength()
        : 0;
    TestFalse(TEXT("readonly preservation begins with a clean package"), bDirtyBefore);

    const auto TestPreserved = [this, Package, bDirtyBefore, UndoQueueBefore](const TCHAR* Flow)
    {
        TestEqual(
            *FString::Printf(TEXT("%s preserves package dirty state"), Flow),
            Package->IsDirty(),
            bDirtyBefore);
        TestEqual(
            *FString::Printf(TEXT("%s preserves Undo history"), Flow),
            GEditor != nullptr && GEditor->Trans != nullptr
                ? GEditor->Trans->GetQueueLength()
                : 0,
            UndoQueueBefore);
    };

    FUnrealMCPBlueprintInspector Inspector;
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    const FString AssetPath = Blueprint->GetPathName();
    TestTrue(
        TEXT("successful readonly inspection executes"),
        Inspector.Execute(AllSectionArguments(AssetPath), Result, Error));
    TestPreserved(TEXT("successful inspection"));

    TestFalse(
        TEXT("rejected readonly inspection remains rejected"),
        Inspector.Execute(
            InspectArguments(TEXT("/Game/UnrealMCPReadonlyTests/Missing.Missing")),
            Result,
            Error));
    TestEqual(TEXT("rejected inspection error is stable"), Error.Code, FString(TEXT("not_found")));
    TestPreserved(TEXT("rejected inspection"));

    UEdGraph* EventGraph =
        !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("readonly timeout fixture has an event graph"), EventGraph))
    {
        return false;
    }
    const TSharedRef<FUnrealMCPRecord> CatalogArguments = MakeShared<FUnrealMCPRecord>();
    CatalogArguments->SetStringField(TEXT("asset_path"), AssetPath);
    CatalogArguments->SetStringField(
        TEXT("graph_id"),
        EventGraph->GraphGuid.ToString(EGuidFormats::Digits).ToLower());
    CatalogArguments->SetStringField(TEXT("expected_snapshot"), InspectSnapshot(Inspector, AssetPath));
    CatalogArguments->SetNumberField(TEXT("limit"), 1);
    double ScanClock = 0.0;
    FUnrealMCPBlueprintActionCatalog TimeoutCatalog(
        Inspector,
        TEXT("11111111111111111111111111111111"),
        [] { return 100.0; },
        [&ScanClock]
        {
            ScanClock += UnrealMCP::ActionScanSeconds + 0.1;
            return ScanClock;
        });
    TestTrue(
        TEXT("readonly catalog timeout returns a bounded result"),
        TimeoutCatalog.Execute(CatalogArguments, Result, Error));
    TestTrue(TEXT("readonly catalog timeout is reported"), Result->GetBoolField(TEXT("timed_out")));
    TestPreserved(TEXT("catalog timeout"));

    const FString BridgeId = TEXT("22222222222222222222222222222222");
    FUnrealMCPOperationLedger Ledger(BridgeId, TEXT("readonly-preservation"), [] { return 200.0; });
    const auto OperationArguments = [](const FString& OperationId)
    {
        const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
        Arguments->SetStringField(TEXT("operation_id"), OperationId);
        return Arguments;
    };
    const auto IdentityArguments = [&BridgeId](const FString& OperationId)
    {
        const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
        Arguments->SetStringField(TEXT("operation_id"), OperationId);
        Arguments->SetStringField(TEXT("bridge_instance_id"), BridgeId);
        return Arguments;
    };

    const FString RejectedId = TEXT("33333333333333333333333333333333");
    TestEqual(
        TEXT("rejected operation admits before native dispatch"),
        Ledger.Admit(TEXT("level_open"), OperationArguments(RejectedId)).Kind,
        EUnrealMCPOperationAdmission::Accepted);
    Ledger.Reject(
        RejectedId,
        FUnrealMCPError{TEXT("stale_precondition"), TEXT("readonly rejection fixture")});
    TestTrue(
        TEXT("rejected retained result lookup resolves"),
        Ledger.Status(IdentityArguments(RejectedId), Result, Error));
    TestEqual(TEXT("rejected result remains retained"), Result->GetStringField(TEXT("state")), FString(TEXT("rejected")));
    TestPreserved(TEXT("rejected retained lookup"));

    const FString CancelledId = TEXT("44444444444444444444444444444444");
    TestEqual(
        TEXT("cancellation fixture admits before dispatch"),
        Ledger.Admit(TEXT("level_open"), OperationArguments(CancelledId)).Kind,
        EUnrealMCPOperationAdmission::Accepted);
    TestTrue(
        TEXT("queued retained operation cancels"),
        Ledger.Cancel(IdentityArguments(CancelledId), Result, Error));
    TestTrue(TEXT("queued cancellation is explicit"), Result->GetBoolField(TEXT("cancelled")));
    TestTrue(
        TEXT("cancelled retained result lookup resolves"),
        Ledger.Status(IdentityArguments(CancelledId), Result, Error));
    TestEqual(TEXT("cancelled result remains retained"), Result->GetStringField(TEXT("state")), FString(TEXT("cancelled")));
    TestPreserved(TEXT("cancellation and retained lookup"));

    const FString CompletedId = TEXT("55555555555555555555555555555555");
    TestEqual(
        TEXT("completed result fixture admits"),
        Ledger.Admit(TEXT("level_open"), OperationArguments(CompletedId)).Kind,
        EUnrealMCPOperationAdmission::Accepted);
    TestTrue(TEXT("completed result fixture executes"), Ledger.MarkExecuting(CompletedId, Error));
    const TSharedRef<FUnrealMCPRecord> RetainedResult = MakeShared<FUnrealMCPRecord>();
    RetainedResult->SetStringField(TEXT("operation_state"), TEXT("committed"));
    Ledger.Complete(CompletedId, TEXT("committed"), RetainedResult);
    TestTrue(
        TEXT("committed retained result lookup resolves"),
        Ledger.Status(IdentityArguments(CompletedId), Result, Error));
    TestEqual(TEXT("committed result remains retained"), Result->GetStringField(TEXT("state")), FString(TEXT("committed")));
    TestPreserved(TEXT("committed retained lookup"));

    FUnrealMCPOperationLedger RestartedLedger(
        TEXT("66666666666666666666666666666666"),
        TEXT("readonly-preservation"),
        [] { return 201.0; });
    TestTrue(
        TEXT("editor restart reconciles old operation identity"),
        RestartedLedger.Status(IdentityArguments(CompletedId), Result, Error));
    TestEqual(
        TEXT("editor restart reports the old retained outcome unknown"),
        Result->GetStringField(TEXT("state")),
        FString(TEXT("outcome_unknown")));
    TestFalse(TEXT("editor restart does not claim an old retained result"), Result->GetBoolField(TEXT("retained")));

    FUnrealMCPBlueprintInspector RestartedInspector;
    TestTrue(
        TEXT("readonly inspection succeeds after editor-service restart"),
        RestartedInspector.Execute(AllSectionArguments(AssetPath), Result, Error));
    TestPreserved(TEXT("editor restart"));
    return true;
}


#endif
