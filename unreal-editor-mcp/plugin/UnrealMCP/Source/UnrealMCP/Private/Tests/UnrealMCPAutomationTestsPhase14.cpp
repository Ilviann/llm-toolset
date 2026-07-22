#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase14GameplayFrameworkFamiliesTest,
    "UnrealMCP.Phase14.GameModeAndGameStateFamilies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase14GameplayFrameworkFamiliesTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    using namespace UnrealMCP::BlueprintFamilyPolicy;

    const TArray<TSharedPtr<FJsonValue>> Matrix = BuildPublishedMatrix();
    TestEqual(TEXT("family matrix retains generic Actor plus all released gameplay families"), Matrix.Num(), 6);
    for (const TSharedPtr<FJsonValue>& Value : Matrix)
    {
        const TSharedPtr<FJsonObject> Record = Value->AsObject();
        if (!TestTrue(TEXT("family matrix records are objects"), Record.IsValid())) return false;
        const TSharedPtr<FJsonObject> Operations = Record->GetObjectField(TEXT("operations"));
        TestTrue(TEXT("published families support the shared authoring path"), Operations->GetBoolField(TEXT("graph_edit")));
        TestFalse(TEXT("Blueprint parent changes stay excluded"), Operations->GetBoolField(TEXT("parent_change")));
        const FString Family = Record->GetStringField(TEXT("family"));
        const bool bSupportsProjectAssignment = Family == TEXT("game_mode_base") || Family == TEXT("game_mode")
            || Family == TEXT("game_instance");
        TestEqual(TEXT("project-settings assignment is published only for assignable framework families"),
            Operations->GetBoolField(TEXT("project_settings_assignment")), bSupportsProjectAssignment);
    }
    TestFalse(TEXT("arbitrary UObject classes stay outside the published family policy"),
        Supports(UObject::StaticClass(), EOperation::Inspect));

    struct FFixture
    {
        UClass* Parent;
        const TCHAR* Family;
        const TCHAR* DefaultProperty;
        TSharedPtr<FJsonValue> DefaultValue;
        const TCHAR* CallableOwner;
        const TCHAR* CallableFunction;
        const TCHAR* CallbackOwner;
        const TCHAR* CallbackFunction;
    };
    const TArray<FFixture> Fixtures = {
        {AGameModeBase::StaticClass(), TEXT("game_mode_base"), TEXT("bUseSeamlessTravel"),
            MakeShared<FJsonValueBoolean>(true), TEXT("/Script/Engine.GameModeBase"), TEXT("GetDefaultPawnClassForController"),
            TEXT("/Script/Engine.GameModeBase"), TEXT("K2_PostLogin")},
        {AGameMode::StaticClass(), TEXT("game_mode"), TEXT("bDelayedStart"),
            MakeShared<FJsonValueBoolean>(true), TEXT("/Script/Engine.GameMode"), TEXT("GetMatchState"),
            TEXT("/Script/Engine.GameMode"), TEXT("K2_OnSetMatchState")},
        {AGameStateBase::StaticClass(), TEXT("game_state_base"), TEXT("ServerWorldTimeSecondsUpdateFrequency"),
            MakeShared<FJsonValueNumber>(0.25), TEXT("/Script/Engine.GameStateBase"), TEXT("GetServerWorldTimeSeconds"),
            TEXT("/Script/Engine.Actor"), TEXT("ReceiveActorBeginOverlap")},
        {AGameState::StaticClass(), TEXT("game_state"), TEXT("ServerWorldTimeSecondsUpdateFrequency"),
            MakeShared<FJsonValueNumber>(0.75), TEXT("/Script/Engine.GameStateBase"), TEXT("GetServerWorldTimeSeconds"),
            TEXT("/Script/Engine.Actor"), TEXT("ReceiveActorBeginOverlap")}};

    const FString Base = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    FUnrealMCPBlueprintActionCatalog Catalog(Inspector, TEXT("14141414141414141414141414141414"));
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    for (int32 Index = 0; Index < Fixtures.Num(); ++Index)
    {
        const FFixture& Fixture = Fixtures[Index];
        const FString PackageName = Base + TEXT("/BP_Family_") + LexToString(Index);
        if (!TestTrue(*FString::Printf(TEXT("%s creation succeeds"), Fixture.Family), Mutator.Execute(
            TEXT("blueprint_create"), CreateArguments(Fixture.Parent->GetPathName(), PackageName), Result, Error)))
        {
            AddError(Error.Code + TEXT(": ") + Error.Message);
            return false;
        }
        const FString AssetPath = Result->GetStringField(TEXT("asset_path"));
        TestEqual(*FString::Printf(TEXT("%s creation is family-aware"), Fixture.Family),
            Result->GetStringField(TEXT("blueprint_family")), FString(Fixture.Family));
        UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *AssetPath);
        if (!TestNotNull(*FString::Printf(TEXT("%s Blueprint loads"), Fixture.Family), Blueprint)) return false;
        TestEqual(*FString::Printf(TEXT("%s live classifier is exact"), Fixture.Family),
            Classify(Blueprint->GeneratedClass).Name, FString(Fixture.Family));

        TSharedRef<FJsonObject> Inspect = InspectArguments(AssetPath);
        Inspect->SetArrayField(TEXT("sections"), {
            MakeShared<FJsonValueString>(TEXT("summary")), MakeShared<FJsonValueString>(TEXT("components")),
            MakeShared<FJsonValueString>(TEXT("functions")), MakeShared<FJsonValueString>(TEXT("local_variables")),
            MakeShared<FJsonValueString>(TEXT("graphs"))});
        if (!TestTrue(*FString::Printf(TEXT("%s inspection succeeds"), Fixture.Family),
            Inspector.Execute(Inspect, Result, Error))) return false;
        TestEqual(*FString::Printf(TEXT("%s inspection is family-aware"), Fixture.Family),
            Result->GetStringField(TEXT("blueprint_family")), FString(Fixture.Family));
        const TSharedPtr<FJsonObject> Live = Result->GetObjectField(TEXT("family_capabilities"));
        TestTrue(TEXT("family class defaults are live"), Live->GetBoolField(TEXT("class_defaults")));
        TestTrue(TEXT("family component authoring is live"), Live->GetBoolField(TEXT("components")));
        TestTrue(TEXT("family event graphs are live"), Live->GetBoolField(TEXT("event_graphs")));
        TestTrue(TEXT("family local variables are supported"), Live->GetBoolField(TEXT("local_variables")));
        TestTrue(TEXT("family overrides are available"), Live->GetBoolField(TEXT("overrides")));

        TSharedRef<FJsonObject> Default = MakeShared<FJsonObject>();
        Default->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
        Default->SetStringField(TEXT("asset_path"), AssetPath);
        Default->SetStringField(TEXT("expected_snapshot"), Result->GetStringField(TEXT("snapshot_id")));
        Default->SetStringField(TEXT("property_name"), Fixture.DefaultProperty);
        Default->SetField(TEXT("value"), Fixture.DefaultValue);
        if (!TestTrue(*FString::Printf(TEXT("%s family default edits"), Fixture.Family),
            Mutator.Execute(TEXT("blueprint_default_edit"), Default, Result, Error)))
        {
            AddError(Error.Code + TEXT(": ") + Error.Message);
            return false;
        }
        TestEqual(TEXT("default result retains family"), Result->GetStringField(TEXT("blueprint_family")), FString(Fixture.Family));

        TSharedRef<FJsonObject> AddComponent = ComponentEditArguments(
            AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("add"));
        AddComponent->SetStringField(TEXT("component_class"), URotatingMovementComponent::StaticClass()->GetPathName());
        AddComponent->SetStringField(TEXT("name"), TEXT("FamilyMovement"));
        if (!TestTrue(*FString::Printf(TEXT("%s component add succeeds"), Fixture.Family),
            Mutator.Execute(TEXT("blueprint_component_edit"), AddComponent, Result, Error)))
        {
            AddError(Error.Code + TEXT(": ") + Error.Message);
            return false;
        }

        TSharedRef<FJsonObject> AddFunction = ScopedMemberEditArguments(
            AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("function"), TEXT("add"));
        AddFunction->SetStringField(TEXT("name"), TEXT("FamilyLogic"));
        AddFunction->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("public"), false, false, {}));
        if (!TestTrue(*FString::Printf(TEXT("%s function add succeeds"), Fixture.Family),
            Mutator.Execute(TEXT("blueprint_member_edit"), AddFunction, Result, Error)))
        {
            AddError(Error.Code + TEXT(": ") + Error.Message);
            return false;
        }
        const FString FunctionId = Result->GetObjectField(TEXT("function"))->GetStringField(TEXT("id"));
        TSharedRef<FJsonObject> AddLocal = ScopedMemberEditArguments(
            AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("local_variable"), TEXT("add"));
        AddLocal->SetStringField(TEXT("function_id"), FunctionId);
        AddLocal->SetStringField(TEXT("name"), TEXT("FamilyCounter"));
        AddLocal->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
        if (!TestTrue(*FString::Printf(TEXT("%s local variable add succeeds"), Fixture.Family),
            Mutator.Execute(TEXT("blueprint_member_edit"), AddLocal, Result, Error)))
        {
            AddError(Error.Code + TEXT(": ") + Error.Message);
            return false;
        }

        UEdGraph* EventGraph = !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
        if (!TestNotNull(*FString::Printf(TEXT("%s event graph remains available"), Fixture.Family), EventGraph)) return false;
        const FString Snapshot = Result->GetStringField(TEXT("snapshot_id"));
        const TSharedRef<FJsonObject> CatalogArguments = MakeShared<FJsonObject>();
        CatalogArguments->SetStringField(TEXT("asset_path"), AssetPath);
        CatalogArguments->SetStringField(TEXT("graph_id"), EventGraph->GraphGuid.ToString(EGuidFormats::Digits).ToLower());
        CatalogArguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
        CatalogArguments->SetStringField(TEXT("node_family"), TEXT("function_call"));
        CatalogArguments->SetStringField(TEXT("owner_class"), Fixture.CallableOwner);
        CatalogArguments->SetStringField(TEXT("function"), Fixture.CallableFunction);
        CatalogArguments->SetNumberField(TEXT("limit"), 5);
        if (!TestTrue(*FString::Printf(TEXT("%s framework action catalogs"), Fixture.Family),
            Catalog.Execute(CatalogArguments, Result, Error))) return false;
        TestEqual(TEXT("catalog result retains family"), Result->GetStringField(TEXT("blueprint_family")), FString(Fixture.Family));
        TestTrue(*FString::Printf(TEXT("%s framework action is available"), Fixture.Family),
            Result->GetIntegerField(TEXT("returned_count")) > 0);
        CatalogArguments->SetStringField(TEXT("node_family"), TEXT("event"));
        CatalogArguments->SetStringField(TEXT("owner_class"), Fixture.CallbackOwner);
        CatalogArguments->SetStringField(TEXT("function"), Fixture.CallbackFunction);
        if (!TestTrue(*FString::Printf(TEXT("%s callback catalogs"), Fixture.Family),
            Catalog.Execute(CatalogArguments, Result, Error))) return false;
        TestTrue(*FString::Printf(TEXT("%s callback override is available"), Fixture.Family),
            Result->GetIntegerField(TEXT("returned_count")) > 0);

        TSharedRef<FJsonObject> Compile = AssetArguments(AssetPath);
        Compile->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
        Compile->SetStringField(TEXT("expected_snapshot"), Snapshot);
        if (!TestTrue(*FString::Printf(TEXT("%s compiles"), Fixture.Family),
            Mutator.Execute(TEXT("blueprint_compile"), Compile, Result, Error))) return false;
        TestTrue(TEXT("family compile succeeds"), Result->GetBoolField(TEXT("compile_succeeded")));
        TSharedRef<FJsonObject> Save = AssetArguments(AssetPath);
        Save->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
        Save->SetStringField(TEXT("expected_snapshot"), Result->GetStringField(TEXT("snapshot_id")));
        if (!TestTrue(*FString::Printf(TEXT("%s saves"), Fixture.Family),
            Mutator.Execute(TEXT("blueprint_save"), Save, Result, Error))) return false;
        TestFalse(TEXT("saved family Blueprint is clean"), Result->GetBoolField(TEXT("package_dirty")));
    }
    return true;
}


#endif
