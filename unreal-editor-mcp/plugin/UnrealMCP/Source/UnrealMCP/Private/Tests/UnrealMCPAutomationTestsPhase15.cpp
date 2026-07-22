#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase15GameInstanceFamilyTest,
    "UnrealMCP.Phase15.GameInstanceFamily",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase15GameInstanceFamilyTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    using namespace UnrealMCP::BlueprintFamilyPolicy;

    const TArray<TSharedPtr<FJsonValue>> Matrix = BuildPublishedMatrix();
    TestEqual(TEXT("family matrix adds GameInstance"), Matrix.Num(), 6);
    TSharedPtr<FJsonObject> GameInstanceRecord;
    for (const TSharedPtr<FJsonValue>& Value : Matrix)
    {
        const TSharedPtr<FJsonObject> Record = Value->AsObject();
        if (Record.IsValid() && Record->GetStringField(TEXT("family")) == TEXT("game_instance"))
        {
            GameInstanceRecord = Record;
            break;
        }
    }
    if (!TestTrue(TEXT("GameInstance matrix record is published"), GameInstanceRecord.IsValid())) return false;
    TestEqual(TEXT("GameInstance has a UObject inheritance category"),
        GameInstanceRecord->GetStringField(TEXT("inheritance_category")), FString(TEXT("uobject_derived")));
    const TSharedPtr<FJsonObject> Operations = GameInstanceRecord->GetObjectField(TEXT("operations"));
    TestTrue(TEXT("GameInstance graph editing is published"), Operations->GetBoolField(TEXT("graph_edit")));
    TestFalse(TEXT("GameInstance component editing is not published"), Operations->GetBoolField(TEXT("components")));
    TestFalse(TEXT("GameInstance parent changes stay excluded"), Operations->GetBoolField(TEXT("parent_change")));
    TestTrue(TEXT("GameInstance project assignment is published"),
        Operations->GetBoolField(TEXT("project_settings_assignment")));
    TestEqual(TEXT("GameInstance classifies exactly"),
        Classify(UGameInstance::StaticClass()).Name, FString(TEXT("game_instance")));
    TestTrue(TEXT("GameInstance supports member authoring"), Supports(UGameInstance::StaticClass(), EOperation::Members));
    TestFalse(TEXT("GameInstance rejects component authoring"), Supports(UGameInstance::StaticClass(), EOperation::Components));
    TestTrue(TEXT("Actor component support remains unchanged"), Supports(AActor::StaticClass(), EOperation::Components));
    TestFalse(TEXT("arbitrary UObject classes remain unsupported"), Supports(UObject::StaticClass(), EOperation::Inspect));

    const FString PackageName = TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_GameInstance");
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    FUnrealMCPBlueprintActionCatalog Catalog(Inspector, TEXT("15151515151515151515151515151515"));
    FUnrealMCPBlueprintGraphEditor GraphEditor(Inspector, Catalog);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    if (!TestTrue(TEXT("GameInstance creation succeeds"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(UGameInstance::StaticClass()->GetPathName(), PackageName), Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const FString AssetPath = Result->GetStringField(TEXT("asset_path"));
    TestEqual(TEXT("creation reports the GameInstance family"),
        Result->GetStringField(TEXT("blueprint_family")), FString(TEXT("game_instance")));
    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!TestNotNull(TEXT("created GameInstance Blueprint loads"), Blueprint)) return false;

    TSharedRef<FJsonObject> Inspect = InspectArguments(AssetPath);
    Inspect->SetArrayField(TEXT("sections"), {
        MakeShared<FJsonValueString>(TEXT("summary")), MakeShared<FJsonValueString>(TEXT("components")),
        MakeShared<FJsonValueString>(TEXT("functions")), MakeShared<FJsonValueString>(TEXT("local_variables")),
        MakeShared<FJsonValueString>(TEXT("graphs"))});
    if (!TestTrue(TEXT("GameInstance inspection succeeds"), Inspector.Execute(Inspect, Result, Error))) return false;
    const FString InitialSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    const TSharedPtr<FJsonObject> Live = Result->GetObjectField(TEXT("family_capabilities"));
    TestTrue(TEXT("GameInstance class defaults are live"), Live->GetBoolField(TEXT("class_defaults")));
    TestFalse(TEXT("GameInstance components are explicitly unavailable"), Live->GetBoolField(TEXT("components")));
    TestTrue(TEXT("GameInstance event graphs are live"), Live->GetBoolField(TEXT("event_graphs")));
    TestTrue(TEXT("GameInstance local variables are live"), Live->GetBoolField(TEXT("local_variables")));
    TestTrue(TEXT("GameInstance Blueprint callbacks are live"), Live->GetBoolField(TEXT("overrides")));
    const TSharedPtr<FJsonObject> GraphTypes = Live->GetObjectField(TEXT("graph_types"));
    TestTrue(TEXT("GameInstance event graphs are editable"), GraphTypes->GetBoolField(TEXT("event")));
    TestTrue(TEXT("GameInstance function graphs are editable"), GraphTypes->GetBoolField(TEXT("function")));
    TestTrue(TEXT("GameInstance macro graphs are editable"), GraphTypes->GetBoolField(TEXT("macro")));
    const TArray<TSharedPtr<FJsonValue>>& InitialRecords = Result->GetArrayField(TEXT("records"));
    bool bActorSummary = true;
    int32 ComponentRecords = 0;
    for (const TSharedPtr<FJsonValue>& Value : InitialRecords)
    {
        const TSharedPtr<FJsonObject> Record = Value->AsObject();
        if (!Record.IsValid()) continue;
        if (Record->GetStringField(TEXT("section")) == TEXT("summary"))
        {
            bActorSummary = Record->GetBoolField(TEXT("actor_blueprint"));
        }
        if (Record->GetStringField(TEXT("section")) == TEXT("component")) ++ComponentRecords;
    }
    TestFalse(TEXT("GameInstance summary is not mislabeled as Actor"), bActorSummary);
    TestEqual(TEXT("GameInstance inspection emits no component records"), ComponentRecords, 0);

    TSharedRef<FJsonObject> Component = ComponentEditArguments(AssetPath, InitialSnapshot, TEXT("add"));
    Component->SetStringField(TEXT("component_class"), URotatingMovementComponent::StaticClass()->GetPathName());
    Component->SetStringField(TEXT("name"), TEXT("InvalidMovement"));
    TestFalse(TEXT("GameInstance component mutation rejects before mutation"),
        Mutator.Execute(TEXT("blueprint_component_edit"), Component, Result, Error));
    TestEqual(TEXT("GameInstance component rejection is stable"), Error.Code, FString(TEXT("invalid_component")));
    TestEqual(TEXT("component rejection preserves the snapshot"), InspectSnapshot(Inspector, AssetPath), InitialSnapshot);

    TSharedRef<FJsonObject> AddState = MemberEditArguments(AssetPath, InitialSnapshot, TEXT("add"));
    AddState->SetStringField(TEXT("name"), TEXT("SessionRegion"));
    AddState->SetObjectField(TEXT("type"), K2Type(TEXT("string")));
    AddState->SetObjectField(TEXT("default"), LiteralDefault(MakeShared<FJsonValueString>(TEXT("offline"))));
    const TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
    Metadata->SetStringField(TEXT("category"), TEXT("Session"));
    Metadata->SetBoolField(TEXT("instance_editable"), true);
    Metadata->SetBoolField(TEXT("blueprint_visible"), true);
    AddState->SetObjectField(TEXT("metadata"), Metadata);
    if (!TestTrue(TEXT("GameInstance persistent state member adds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddState, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }

    TSharedRef<FJsonObject> CompileMember = AssetArguments(AssetPath);
    CompileMember->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    CompileMember->SetStringField(TEXT("expected_snapshot"), Result->GetStringField(TEXT("snapshot_id")));
    if (!TestTrue(TEXT("GameInstance member compiles"),
        Mutator.Execute(TEXT("blueprint_compile"), CompileMember, Result, Error))) return false;
    TSharedRef<FJsonObject> Default = MakeShared<FJsonObject>();
    Default->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Default->SetStringField(TEXT("asset_path"), AssetPath);
    Default->SetStringField(TEXT("expected_snapshot"), Result->GetStringField(TEXT("snapshot_id")));
    Default->SetStringField(TEXT("property_name"), TEXT("SessionRegion"));
    Default->SetStringField(TEXT("value"), TEXT("eu-central"));
    if (!TestTrue(TEXT("GameInstance session default edits"),
        Mutator.Execute(TEXT("blueprint_default_edit"), Default, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }

    TSharedRef<FJsonObject> AddFunction = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("function"), TEXT("add"));
    AddFunction->SetStringField(TEXT("name"), TEXT("ResetSession"));
    AddFunction->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("public"), false, false, {}));
    if (!TestTrue(TEXT("GameInstance function adds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddFunction, Result, Error))) return false;
    const FString FunctionId = Result->GetObjectField(TEXT("function"))->GetStringField(TEXT("id"));
    TSharedRef<FJsonObject> AddLocal = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("local_variable"), TEXT("add"));
    AddLocal->SetStringField(TEXT("function_id"), FunctionId);
    AddLocal->SetStringField(TEXT("name"), TEXT("PreviousRegion"));
    AddLocal->SetObjectField(TEXT("type"), K2Type(TEXT("string")));
    if (!TestTrue(TEXT("GameInstance function local adds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddLocal, Result, Error))) return false;

    UEdGraph* EventGraph = !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("GameInstance has a local event graph"), EventGraph)) return false;
    const FString EventGraphId = EventGraph->GraphGuid.ToString(EGuidFormats::Digits).ToLower();
    const FString CatalogSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    const TSharedRef<FJsonObject> CatalogArguments = MakeShared<FJsonObject>();
    CatalogArguments->SetStringField(TEXT("asset_path"), AssetPath);
    CatalogArguments->SetStringField(TEXT("graph_id"), EventGraphId);
    CatalogArguments->SetStringField(TEXT("expected_snapshot"), CatalogSnapshot);
    CatalogArguments->SetStringField(TEXT("node_family"), TEXT("event"));
    CatalogArguments->SetStringField(TEXT("owner_class"), TEXT("/Script/Engine.GameInstance"));
    CatalogArguments->SetStringField(TEXT("function"), TEXT("ReceiveInit"));
    CatalogArguments->SetNumberField(TEXT("limit"), 5);
    if (!TestTrue(TEXT("GameInstance Init callback catalogs"), Catalog.Execute(CatalogArguments, Result, Error))) return false;
    TestEqual(TEXT("GameInstance callback catalog retains family"),
        Result->GetStringField(TEXT("blueprint_family")), FString(TEXT("game_instance")));
    const TArray<TSharedPtr<FJsonValue>>& Actions = Result->GetArrayField(TEXT("actions"));
    if (!TestTrue(TEXT("GameInstance Init callback is available"), !Actions.IsEmpty())) return false;
    const FString ActionId = Actions[0]->AsObject()->GetStringField(TEXT("action_id"));

    const TSharedRef<FJsonObject> Position = MakeShared<FJsonObject>();
    Position->SetNumberField(TEXT("x"), 160);
    Position->SetNumberField(TEXT("y"), 120);
    const TSharedRef<FJsonObject> AddCallback = MakeShared<FJsonObject>();
    AddCallback->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    AddCallback->SetStringField(TEXT("asset_path"), AssetPath);
    AddCallback->SetStringField(TEXT("expected_snapshot"), CatalogSnapshot);
    AddCallback->SetStringField(TEXT("operation"), TEXT("add_node"));
    AddCallback->SetStringField(TEXT("graph_id"), EventGraphId);
    AddCallback->SetStringField(TEXT("action_id"), ActionId);
    AddCallback->SetObjectField(TEXT("position"), Position);
    if (!TestTrue(TEXT("GameInstance Init callback node adds"), GraphEditor.Execute(AddCallback, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestEqual(TEXT("GameInstance graph result retains family"),
        Result->GetStringField(TEXT("blueprint_family")), FString(TEXT("game_instance")));

    TSharedRef<FJsonObject> Compile = AssetArguments(AssetPath);
    Compile->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Compile->SetStringField(TEXT("expected_snapshot"), Result->GetStringField(TEXT("snapshot_id")));
    if (!TestTrue(TEXT("GameInstance logic compiles"),
        Mutator.Execute(TEXT("blueprint_compile"), Compile, Result, Error))) return false;
    TestTrue(TEXT("GameInstance compile succeeds"), Result->GetBoolField(TEXT("compile_succeeded")));
    TSharedRef<FJsonObject> Save = AssetArguments(AssetPath);
    Save->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Save->SetStringField(TEXT("expected_snapshot"), Result->GetStringField(TEXT("snapshot_id")));
    if (!TestTrue(TEXT("GameInstance saves"), Mutator.Execute(TEXT("blueprint_save"), Save, Result, Error))) return false;
    TestFalse(TEXT("saved GameInstance is clean"), Result->GetBoolField(TEXT("package_dirty")));

    TSharedRef<FJsonObject> ReadBack = InspectArguments(AssetPath);
    ReadBack->SetArrayField(TEXT("sections"), {
        MakeShared<FJsonValueString>(TEXT("components")), MakeShared<FJsonValueString>(TEXT("class_defaults")),
        MakeShared<FJsonValueString>(TEXT("functions")), MakeShared<FJsonValueString>(TEXT("local_variables")),
        MakeShared<FJsonValueString>(TEXT("graphs")), MakeShared<FJsonValueString>(TEXT("nodes"))});
    ReadBack->SetArrayField(TEXT("property_names"), {MakeShared<FJsonValueString>(TEXT("SessionRegion"))});
    if (!TestTrue(TEXT("GameInstance read-back succeeds"), Inspector.Execute(ReadBack, Result, Error))) return false;
    bool bFoundDefault = false;
    for (const TSharedPtr<FJsonValue>& Value : Result->GetArrayField(TEXT("records")))
    {
        const TSharedPtr<FJsonObject> Record = Value->AsObject();
        if (Record.IsValid() && Record->GetStringField(TEXT("section")) == TEXT("class_default")
            && Record->GetStringField(TEXT("name")) == TEXT("SessionRegion")
            && Record->GetStringField(TEXT("value")) == TEXT("eu-central"))
        {
            bFoundDefault = true;
        }
    }
    TestTrue(TEXT("GameInstance session default reads back"), bFoundDefault);
    return true;
}


#endif
