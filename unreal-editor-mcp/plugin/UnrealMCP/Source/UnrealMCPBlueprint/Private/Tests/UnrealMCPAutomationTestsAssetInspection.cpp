#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPBlueprintAutomationTestSupport.h"
#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPAssetInspectionAdapters.h"

#include "Components/ActorComponent.h"
#include "Blueprint/UserWidget.h"
#include "Engine/DataAsset.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "UObject/Interface.h"
#include "WidgetBlueprint.h"

namespace
{
TSharedRef<FUnrealMCPRecord> Request(const FString& Path, const FString& Selector = FString())
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(TEXT("asset_path"), Path);
    if (!Selector.IsEmpty()) Arguments->SetStringField(TEXT("selector"), Selector);
    return Arguments;
}

UBlueprint* CreateTypedBlueprint(const FString& PackageName, UClass* Parent, EBlueprintType Type = BPTYPE_Normal)
{
    UPackage* Package = CreatePackage(*PackageName);
    const FString Name = FPackageName::GetLongPackageAssetName(PackageName);
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        Parent, Package, FName(*Name), Type, FName(TEXT("UnrealMCP.AssetInspect")));
    if (Blueprint != nullptr) FAssetRegistryModule::AssetCreated(Blueprint);
    return Blueprint;
}

UEdGraph* AddFunction(UBlueprint* Blueprint, const FName Name)
{
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint, Name, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, Graph, true, nullptr);
    return Graph;
}

FString AssetType(const TSharedPtr<FUnrealMCPRecord>& Result)
{
    return Result.IsValid() ? Result->GetObjectField(TEXT("asset"))->GetStringField(TEXT("type")) : FString();
}

class FIsolatedTextureInspectionAdapter final : public IUnrealMCPAssetFamilyInspectionAdapter
{
public:
    bool Inspect(
        const FUnrealMCPAssetFamilyInspectionContext&,
        FUnrealMCPAssetFamilyDocumentBuilder&,
        FUnrealMCPAssetFamilySelectorRouter&,
        FUnrealMCPAssetFamilySnapshotBuilder&,
        FUnrealMCPError&) override
    {
        return true;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPAssetInspectionAdapterIsolationTest,
    "UnrealMCP.AssetInspect.AdapterIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPAssetInspectionAdapterIsolationTest::RunTest(const FString& Parameters)
{
    FUnrealMCPAssetFamilyRegistry Registry;
    FUnrealMCPError Error;
    if (!TestTrue(TEXT("built-in adapters register"),
        UnrealMCP::AssetInspection::RegisterBuiltInAdapters(Registry, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }

    FUnrealMCPAssetFamilyDescriptor Texture;
    Texture.FamilyId = TEXT("isolated_texture");
    Texture.NativeClass = UTexture2D::StaticClass();
    Texture.ClassPolicy = EUnrealMCPAssetFamilyClassPolicy::Exact;
    Texture.Priority = 200;
    Texture.Capabilities.bInspection = true;
    Texture.InspectionAdapter = MakeShared<FIsolatedTextureInspectionAdapter>();
    if (!TestTrue(TEXT("isolated family registers without coordinator changes"), Registry.Register(MoveTemp(Texture), Error))
        || !TestTrue(TEXT("extended registry freezes"), Registry.Freeze(Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }

    FUnrealMCPAssetFamilySelection Selection;
    if (!TestTrue(TEXT("Blueprint selection remains available"), Registry.Select(
            UBlueprint::StaticClass(), EUnrealMCPAssetFamilyCapability::Inspection, Selection, Error)))
    {
        return false;
    }
    TestEqual(TEXT("unrelated family does not replace core Blueprints"),
        Selection.Descriptor->FamilyId, FString(TEXT("core_blueprint")));

    if (!TestTrue(TEXT("derived Blueprint storage selects core adapter"), Registry.Select(
            UWidgetBlueprint::StaticClass(), EUnrealMCPAssetFamilyCapability::Inspection, Selection, Error)))
    {
        return false;
    }
    TestEqual(TEXT("Widget Blueprint storage remains a neutral core Blueprint response"),
        Selection.Descriptor->FamilyId, FString(TEXT("core_blueprint")));

    if (!TestTrue(TEXT("isolated exact family selects"), Registry.Select(
            UTexture2D::StaticClass(), EUnrealMCPAssetFamilyCapability::Inspection, Selection, Error)))
    {
        return false;
    }
    TestEqual(TEXT("exact family overrides only its neutral fallback"),
        Selection.Descriptor->FamilyId, FString(TEXT("isolated_texture")));

    if (!TestTrue(TEXT("neutral fallback remains available"), Registry.Select(
            UObject::StaticClass(), EUnrealMCPAssetFamilyCapability::Inspection, Selection, Error)))
    {
        return false;
    }
    TestEqual(TEXT("unsupported families remain neutral"),
        Selection.Descriptor->FamilyId, FString(TEXT("neutral_asset")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPAssetInspectionCoreTest,
    "UnrealMCP.AssetInspect.CoreFamiliesSelectorsPagingAndLimits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPAssetInspectionCoreTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString Root = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const TSharedRef<FUnrealMCPAssetFamilyRegistry> FamilyRegistry = MakeShared<FUnrealMCPAssetFamilyRegistry>();
    FUnrealMCPError Error;
    if (!TestTrue(TEXT("built-in inspection adapters register"),
            UnrealMCP::AssetInspection::RegisterBuiltInAdapters(*FamilyRegistry, Error))
        || !TestTrue(TEXT("built-in inspection adapters freeze"), FamilyRegistry->Freeze(Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    FUnrealMCPAssetInspectionService Service(FamilyRegistry);
    TSharedPtr<FUnrealMCPRecord> Result;

    struct FFamily { UClass* Parent; const TCHAR* Name; };
    const TArray<FFamily> Families = {
        {AActor::StaticClass(), TEXT("actor_blueprint")},
        {AGameModeBase::StaticClass(), TEXT("game_mode_base_blueprint")},
        {AGameMode::StaticClass(), TEXT("game_mode_blueprint")},
        {AGameStateBase::StaticClass(), TEXT("game_state_base_blueprint")},
        {AGameState::StaticClass(), TEXT("game_state_blueprint")},
        {UGameInstance::StaticClass(), TEXT("game_instance_blueprint")},
        {UPrimaryDataAsset::StaticClass(), TEXT("primary_data_asset_blueprint")},
        {APlayerController::StaticClass(), TEXT("player_controller_blueprint")},
        {APlayerState::StaticClass(), TEXT("player_state_blueprint")},
        {UActorComponent::StaticClass(), TEXT("actor_component_blueprint")},
        {UUserWidget::StaticClass(), TEXT("widget_blueprint")},
    };
    int32 FamilyIndex = 0;
    for (const FFamily& Family : Families)
    {
        const FString Package = Root + TEXT("/BP_Family_") + LexToString(++FamilyIndex);
        UBlueprint* Blueprint = CreateTypedBlueprint(Package, Family.Parent);
        if (!TestNotNull(TEXT("family Blueprint creates"), Blueprint)) return false;
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        if (!TestTrue(TEXT("family root inspects"), Service.Execute(Request(Package), Result, Error)))
        {
            AddError(Error.Code + TEXT(": ") + Error.Message);
            return false;
        }
        TestEqual(TEXT("family classification is exact"), AssetType(Result), FString(Family.Name));
        TestEqual(TEXT("package path canonicalizes"),
            Result->GetObjectField(TEXT("asset"))->GetStringField(TEXT("path")),
            Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package));
        TestTrue(TEXT("snapshot is a stable SHA-1 identity"), Result->GetStringField(TEXT("snapshot_id")).Len() == 40);
    }

    const FString ActorPackage = Root + TEXT("/BP_ActorSemantic");
    UBlueprint* ActorBlueprint = CreateTypedBlueprint(ActorPackage, AActor::StaticClass());
    if (!TestNotNull(TEXT("semantic Actor Blueprint creates"), ActorBlueprint)) return false;
    AddFunction(ActorBlueprint, FName(TEXT("Функция")));
    FKismetEditorUtilities::CompileBlueprint(ActorBlueprint);
    AActor* Defaults = Cast<AActor>(ActorBlueprint->GeneratedClass->GetDefaultObject(false));
    Defaults->Tags = {TEXT("alpha"), TEXT("тег"), TEXT("omega")};
    const FString ActorPath = ActorPackage + TEXT(".BP_ActorSemantic");
    if (!TestTrue(TEXT("actor semantic root inspects"), Service.Execute(Request(ActorPath), Result, Error))) return false;
    const FString StableSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedPtr<FUnrealMCPRecord> PackageForm;
    if (!TestTrue(TEXT("package form inspects"), Service.Execute(Request(ActorPackage), PackageForm, Error))) return false;
    TestEqual(TEXT("package and object forms share snapshot"), PackageForm->GetStringField(TEXT("snapshot_id")), StableSnapshot);

    const FString Utf8Selector = TEXT("functions/%D0%A4%D1%83%D0%BD%D0%BA%D1%86%D0%B8%D1%8F");
    if (!TestTrue(TEXT("UTF-8 canonical selector resolves"), Service.Execute(Request(ActorPath, Utf8Selector), Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestEqual(TEXT("selected function keeps exact decoded name"),
        Result->GetObjectField(TEXT("function"))->GetStringField(TEXT("name")), FString(TEXT("Функция")));
    TestTrue(TEXT("complete selected function is marked complete"),
        Result->GetObjectField(TEXT("graph"))->GetObjectField(TEXT("graph_status"))->GetBoolField(TEXT("complete")));

    TSharedRef<FUnrealMCPRecord> TagPage = Request(ActorPath, TEXT("properties/actor/tags"));
    TagPage->SetNumberField(TEXT("page_size"), 2);
    TagPage->SetNumberField(TEXT("page_index"), 1);
    if (!TestTrue(TEXT("zero-based tag page inspects"), Service.Execute(TagPage, Result, Error))) return false;
    const TSharedPtr<FUnrealMCPRecord> Collection = Result->GetObjectField(TEXT("collection"));
    TestEqual(TEXT("tag page retains total count"), Collection->GetIntegerField(TEXT("count")), 3);
    TestEqual(TEXT("tag page index is zero-based"), Collection->GetIntegerField(TEXT("page_index")), 1);
    TestEqual(TEXT("last tag page has one item"), Collection->GetArrayField(TEXT("items")).Num(), 1);

    TSharedRef<FUnrealMCPRecord> InvalidGraphPaging = Request(ActorPath, Utf8Selector);
    InvalidGraphPaging->SetNumberField(TEXT("page_index"), 0);
    TestFalse(TEXT("graph paging rejects"), Service.Execute(InvalidGraphPaging, Result, Error));
    TestEqual(TEXT("graph paging error is stable"), Error.Code, FString(TEXT("invalid_argument")));
    TSharedRef<FUnrealMCPRecord> InvalidPartialRoot = Request(ActorPath);
    InvalidPartialRoot->SetBoolField(TEXT("allow_partial_graph"), false);
    TestFalse(TEXT("partial flag on root rejects"), Service.Execute(InvalidPartialRoot, Result, Error));
    TestEqual(TEXT("partial root error is stable"), Error.Code, FString(TEXT("invalid_argument")));

    for (const FString& BadPath : {TEXT("/Engine/EngineMaterials/DefaultMaterial"), TEXT("/Game/../Secret"),
        TEXT("/Game/Foo\\Bar"), TEXT("/Game/Foo.BarOther")})
    {
        TestFalse(TEXT("unsafe or non-canonical path rejects"), Service.Execute(Request(BadPath), Result, Error));
        TestEqual(TEXT("path rejection is invalid_argument"), Error.Code, FString(TEXT("invalid_argument")));
    }
    TSharedRef<FUnrealMCPRecord> BadSelector = Request(ActorPath, TEXT("functions/%d0%a4"));
    TestFalse(TEXT("non-canonical selector rejects"), Service.Execute(BadSelector, Result, Error));
    TestEqual(TEXT("selector rejection is invalid_argument"), Error.Code, FString(TEXT("invalid_argument")));

    const FString InterfacePackage = Root + TEXT("/BPI_Contract");
    UBlueprint* Interface = CreateTypedBlueprint(InterfacePackage, UInterface::StaticClass(), BPTYPE_Interface);
    if (!TestNotNull(TEXT("interface Blueprint creates"), Interface)) return false;
    AddFunction(Interface, FName(TEXT("CanInteract")));
    FKismetEditorUtilities::CompileBlueprint(Interface);
    if (!TestTrue(TEXT("interface root inspects"), Service.Execute(Request(InterfacePackage), Result, Error))) return false;
    TestEqual(TEXT("interface classifies distinctly"), AssetType(Result), FString(TEXT("interface_blueprint")));
    if (!TestTrue(TEXT("interface declaration selects"),
        Service.Execute(Request(InterfacePackage, TEXT("functions/CanInteract")), Result, Error))) return false;
    TestTrue(TEXT("interface selector returns declaration"), Result->HasTypedField<EUnrealMCPValueType::Record>(TEXT("interface_function")));
    TestFalse(TEXT("interface selector fabricates no graph"), Result->HasField(TEXT("graph")));

    const FString TexturePackage = Root + TEXT("/T_Media");
    UPackage* TextureOuter = CreatePackage(*TexturePackage);
    UTexture2D* Texture = NewObject<UTexture2D>(TextureOuter, TEXT("T_Media"), RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(Texture);
    if (!TestTrue(TEXT("media identity inspects"), Service.Execute(Request(TexturePackage), Result, Error))) return false;
    TestEqual(TEXT("media stays neutral"), AssetType(Result), FString(TEXT("asset")));
    TestFalse(TEXT("media has no selectors"), Result->HasField(TEXT("selectors")));

    const FString OversizedPackage = Root + TEXT("/BP_Oversized");
    UBlueprint* Oversized = CreateTypedBlueprint(OversizedPackage, AActor::StaticClass());
    if (!TestNotNull(TEXT("oversized Blueprint creates"), Oversized)) return false;
    UEdGraph* HugeGraph = AddFunction(Oversized, FName(TEXT("HugeGraph")));
    for (int32 Index = 0; Index < 1900; ++Index)
    {
        UEdGraphNode* Node = NewObject<UEdGraphNode>(HugeGraph);
        Node->CreateNewGuid();
        Node->NodePosX = Index * 10;
        HugeGraph->AddNode(Node, false, false);
    }
    const FString HugeSelector = TEXT("functions/HugeGraph");
    TestFalse(TEXT("oversized complete graph rejects"),
        Service.Execute(Request(OversizedPackage, HugeSelector), Result, Error));
    TestEqual(TEXT("oversized graph uses data_limit_exceeded"), Error.Code, FString(TEXT("data_limit_exceeded")));
    TSharedRef<FUnrealMCPRecord> Partial = Request(OversizedPackage, HugeSelector);
    Partial->SetBoolField(TEXT("allow_partial_graph"), true);
    if (!TestTrue(TEXT("oversized graph supports explicit partial slice"), Service.Execute(Partial, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const TSharedPtr<FUnrealMCPRecord> Status = Result->GetObjectField(TEXT("graph"))->GetObjectField(TEXT("graph_status"));
    TestFalse(TEXT("partial graph is explicitly incomplete"), Status->GetBoolField(TEXT("complete")));
    TestEqual(TEXT("partial graph reason is explicit"), Status->GetStringField(TEXT("reason")), FString(TEXT("graph_limit_exceeded")));
    TestTrue(TEXT("partial graph returns fewer detailed nodes"),
        Status->GetIntegerField(TEXT("detailed_nodes")) < Status->GetIntegerField(TEXT("total_nodes")));

    const FString BeforeChange = StableSnapshot;
    FEdGraphPinType IntegerType;
    IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
    FBlueprintEditorUtils::AddMemberVariable(ActorBlueprint, TEXT("ChangedValue"), IntegerType, TEXT("7"));
    if (!TestTrue(TEXT("changed Blueprint re-inspects"), Service.Execute(Request(ActorPath), Result, Error))) return false;
    TestNotEqual(TEXT("snapshot changes with semantic content"), Result->GetStringField(TEXT("snapshot_id")), BeforeChange);
    return true;
}

#endif
