#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimLayerInterface.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "UObject/Interface.h"

namespace UnrealMCP::Tests::Interfaces
{
UBlueprint* CreateFixture(const FString& PackageName)
{
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        UInterface::StaticClass(), CreatePackage(*PackageName),
        FName(*FPackageName::GetLongPackageAssetName(PackageName)), BPTYPE_Interface,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
        TEXT("UnrealMCP.Interfaces"));
    if (Blueprint == nullptr) return nullptr;
    FAssetRegistryModule::AssetCreated(Blueprint);
    for (const TCHAR* Name : {TEXT("GetValue"), TEXT("Notify")})
    {
        UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
            Blueprint, FName(Name), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
        TArray<UK2Node_FunctionEntry*> Entries;
        Graph->GetNodesOfClass(Entries);
        if (Entries.Num() != 1) return nullptr;
        FEdGraphPinType PinType;
        PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
        Entries[0]->CreateUserDefinedPin(TEXT("Count"), PinType, EGPD_Output);
        if (FCString::Strcmp(Name, TEXT("GetValue")) == 0)
        {
            FGraphNodeCreator<UK2Node_FunctionResult> Creator(*Graph);
            UK2Node_FunctionResult* Result = Creator.CreateNode();
            Creator.Finalize();
            Result->CreateUserDefinedPin(TEXT("Value"), PinType, EGPD_Input);
        }
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Blueprint;
}

TSharedPtr<FJsonObject> Find(const TSharedPtr<FJsonObject>& Result, const FString& Section, const FString& Name = FString())
{
    for (const TSharedPtr<FJsonValue>& Item : Result->GetArrayField(TEXT("records")))
    {
        const TSharedPtr<FJsonObject> Record = Item->AsObject();
        if (Record->GetStringField(TEXT("section")) == Section
            && (Name.IsEmpty() || Record->GetStringField(TEXT("name")) == Name)) return Record;
    }
    return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPInterfaceInspectionTest,
    "UnrealMCP.Interfaces.InterfaceInspection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPInterfaceInspectionTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    using namespace UnrealMCP::Tests::Interfaces;
    using namespace UnrealMCP::BlueprintFamilyPolicy;
    const FString Base = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UBlueprint* Blueprint = CreateFixture(Base + TEXT("/BPI_Inspection"));
    if (!TestNotNull(TEXT("interface fixture creates"), Blueprint)) return false;
    TestEqual(TEXT("loaded interface classification"), ClassifyForInspection(Blueprint).Name, FString(TEXT("interface")));
    TestEqual(TEXT("unloaded type classification needs no native parent"),
        ClassifyForInspection(TEXT("BPTYPE_Interface"), nullptr).Name, FString(TEXT("interface")));
    TestFalse(TEXT("native interface class does not grant creation"), Supports(UInterface::StaticClass(), EOperation::Create));
    UAnimBlueprint* AnimationInterface = NewObject<UAnimBlueprint>();
    AnimationInterface->BlueprintType = BPTYPE_Interface;
    AnimationInterface->ParentClass = UAnimLayerInterface::StaticClass();
    TestEqual(TEXT("animation layer interfaces retain their specialized family"),
        ClassifyForInspection(AnimationInterface).Name, FString(TEXT("animation")));
    TestEqual(TEXT("unloaded animation layer interfaces retain their specialized family"),
        ClassifyForInspection(TEXT("BPTYPE_Interface"), UAnimLayerInterface::StaticClass()).Name, FString(TEXT("animation")));
    for (const auto& Item : BuildPublishedMatrix())
    {
        const auto Family = Item->AsObject();
        if (Family->GetStringField(TEXT("family")) != TEXT("interface")) continue;
        for (const auto& Operation : Family->GetObjectField(TEXT("operations"))->Values)
            TestEqual(TEXT("interface operation matrix"), Operation.Value->AsBool(),
                Operation.Key == TEXT("discover") || Operation.Key == TEXT("inspect"));
        TestEqual(TEXT("no interface RPC modes"), Family->GetObjectField(TEXT("multiplayer"))->GetArrayField(TEXT("rpc_modes")).Num(), 0);
    }
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    auto Execute = [&](const TSharedRef<FJsonObject>& Args)
    {
        const bool bSuccess = Inspector.Execute(Args, Result, Error);
        if (!bSuccess) AddError(Error.Code + TEXT(": ") + Error.Message);
        return bSuccess;
    };
    const bool bDirtyBefore = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBefore = Blueprint->Status;
    auto Discover = MakeShared<FJsonObject>();
    Discover->SetStringField(TEXT("mode"), TEXT("discover"));
    Discover->SetStringField(TEXT("package_path"), Base);
    if (!Execute(Discover)) return false;
    auto Asset = Find(Result, TEXT("asset"));
    if (!TestTrue(TEXT("interface discovered"), Asset.IsValid())) return false;
    TestEqual(TEXT("discovery family"), Asset->GetStringField(TEXT("blueprint_family")), FString(TEXT("interface")));
    auto Args = AllSectionArguments(Blueprint->GetPathName());
    Args->SetBoolField(TEXT("include_inherited"), true);
    if (!Execute(Args)) return false;
    const FString Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TestEqual(TEXT("exact family"), Result->GetStringField(TEXT("blueprint_family")), FString(TEXT("interface")));
    const auto Caps = Result->GetObjectField(TEXT("family_capabilities"));
    for (const TCHAR* Name : {TEXT("class_defaults"), TEXT("components"), TEXT("widget_tree"),
        TEXT("event_graphs"), TEXT("local_variables"), TEXT("overrides")})
        TestFalse(Name, Caps->GetBoolField(Name));
    TestTrue(TEXT("function graphs inspectable"), Caps->GetObjectField(TEXT("graph_types"))->GetBoolField(TEXT("function")));
    TestFalse(TEXT("not Actor blueprint"), Find(Result, TEXT("summary"))->GetBoolField(TEXT("actor_blueprint")));
    for (const TCHAR* Name : {TEXT("GetValue"), TEXT("Notify")})
    {
        const auto Function = Find(Result, TEXT("function"), Name);
        const auto Graph = Find(Result, TEXT("graph"), Name);
        if (!TestTrue(TEXT("declaration and graph listed"), Function.IsValid() && Graph.IsValid())) return false;
        TestEqual(TEXT("interface declaration ownership"), Function->GetStringField(TEXT("ownership")), FString(TEXT("interface")));
        TestFalse(TEXT("declaration non-editable"), Function->GetBoolField(TEXT("editable")));
        TestFalse(TEXT("declaration non-replaceable"), Function->GetObjectField(TEXT("replacement_boundary"))->GetBoolField(TEXT("replaceable")));
        TestTrue(TEXT("void and output declarations have valid required nodes"), Function->GetObjectField(TEXT("required_nodes"))->GetBoolField(TEXT("valid")));
        const auto& Signature = Function->GetObjectField(TEXT("signature"))->GetArrayField(TEXT("parameters"));
        TestEqual(TEXT("input and output signature counts"), Signature.Num(), FCString::Strcmp(Name, TEXT("GetValue")) == 0 ? 2 : 1);
        TestTrue(TEXT("graph signature equals declaration"), FJsonValue::CompareEqual(
            FJsonValueArray(Signature), FJsonValueArray(Graph->GetArrayField(TEXT("parameters")))));
    }
    auto Function = Find(Result, TEXT("function"), TEXT("GetValue"));
    auto Selected = InspectArguments(Blueprint->GetPathName());
    Selected->SetStringField(TEXT("function_id"), Function->GetStringField(TEXT("id")));
    Selected->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("functions")), MakeShared<FJsonValueString>(TEXT("parameters"))});
    if (!Execute(Selected)) return false;
    TestEqual(TEXT("exact function returns only its signature"), Result->GetIntegerField(TEXT("record_count")), 3);
    Selected = InspectArguments(Blueprint->GetPathName());
    Selected->SetStringField(TEXT("graph_name"), TEXT("GetValue"));
    Selected->SetNumberField(TEXT("page_size"), 1);
    if (!Execute(Selected)) return false;
    TestEqual(TEXT("selection preserves snapshot"), Result->GetStringField(TEXT("snapshot_id")), Snapshot);
    auto Cursor = MakeShared<FJsonObject>();
    Cursor->SetStringField(TEXT("cursor"), Result->GetStringField(TEXT("next_cursor")));
    if (!Execute(Cursor)) return false;
    TestEqual(TEXT("cursor preserves family"), Result->GetStringField(TEXT("blueprint_family")), FString(TEXT("interface")));
    TestEqual(TEXT("cursor preserves snapshot"), Result->GetStringField(TEXT("snapshot_id")), Snapshot);
    for (const TCHAR* Operation : {TEXT("blueprint_compile"), TEXT("blueprint_save")})
    {
        TestFalse(TEXT("interface mutation rejects"), Mutator.Execute(Operation, AssetArguments(Blueprint->GetPathName()), Result, Error));
        TestEqual(TEXT("stable rejection"), Error.Code, FString(TEXT("wrong_type")));
    }
    TestEqual(TEXT("inspection and rejection preserve snapshot"), InspectSnapshot(Inspector, Blueprint->GetPathName()), Snapshot);
    TestEqual(TEXT("inspection preserves dirtiness"), Blueprint->GetOutermost()->IsDirty(), bDirtyBefore);
    TestEqual(TEXT("inspection preserves compile state"), Blueprint->Status, StatusBefore);
    if (!Execute(Selected)) return false;
    Cursor->SetStringField(TEXT("cursor"), Result->GetStringField(TEXT("next_cursor")));
    ++Blueprint->FunctionGraphs.Last()->Nodes[0]->NodePosX;
    TestFalse(TEXT("unselected declaration change invalidates cursor"), Inspector.Execute(Cursor, Result, Error));
    TestEqual(TEXT("stale cursor rejection"), Error.Code, FString(TEXT("stale_precondition")));
    Selected->SetStringField(TEXT("graph_name"), TEXT("Missing"));
    TestFalse(TEXT("missing graph rejects"), Inspector.Execute(Selected, Result, Error));
    TestEqual(TEXT("missing graph error"), Error.Code, FString(TEXT("not_found")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPInterfaceLiveFixtureTest,
    "UnrealMCP.Interfaces.InterfaceLiveFixture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPInterfaceLiveFixtureTest::RunTest(const FString& Parameters)
{
    UBlueprint* Blueprint = UnrealMCP::Tests::Interfaces::CreateFixture(TEXT("/Game/UnrealMCPTests/BPI_InterfaceFixture"));
    return TestNotNull(TEXT("live interface creates"), Blueprint)
        && TestTrue(TEXT("live interface saves"), UnrealMCP::Tests::SaveBlueprintFixture(Blueprint));
}

#endif
