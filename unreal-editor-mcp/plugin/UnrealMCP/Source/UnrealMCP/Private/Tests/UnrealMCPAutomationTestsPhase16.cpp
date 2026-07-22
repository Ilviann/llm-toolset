#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

namespace
{
TSharedRef<FJsonObject> EmptyEventSignature()
{
    const TSharedRef<FJsonObject> Signature = MakeShared<FJsonObject>();
    Signature->SetArrayField(TEXT("parameters"), {});
    return Signature;
}

FString EventGraphId(UBlueprint* Blueprint)
{
    return Blueprint != nullptr && !Blueprint->UbergraphPages.IsEmpty() && Blueprint->UbergraphPages[0] != nullptr
        ? Blueprint->UbergraphPages[0]->GraphGuid.ToString(EGuidFormats::Digits).ToLower() : FString();
}

TSharedRef<FJsonObject> FrameworkArguments(
    const FString& ProjectHash,
    const FString& Setting,
    const FString& ClassPath,
    const FString& Expected)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("project_hash"), ProjectHash);
    Arguments->SetStringField(TEXT("setting"), Setting);
    Arguments->SetStringField(TEXT("class_path"), ClassPath);
    Arguments->SetStringField(TEXT("expected_class"), Expected);
    return Arguments;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase16MultiplayerAuthoringTest,
    "UnrealMCP.Phase16.MultiplayerAuthoring",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase16MultiplayerAuthoringTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    using namespace UnrealMCP::BlueprintFamilyPolicy;

    const TArray<TSharedPtr<FJsonValue>> Matrix = BuildPublishedMatrix();
    TestEqual(TEXT("family matrix remains bounded"), Matrix.Num(), 6);
    for (const TSharedPtr<FJsonValue>& Value : Matrix)
    {
        const TSharedPtr<FJsonObject> Record = Value->AsObject();
        if (!Record.IsValid()) continue;
        const FString Family = Record->GetStringField(TEXT("family"));
        const TSharedPtr<FJsonObject> Operations = Record->GetObjectField(TEXT("operations"));
        const bool bAssignable = Family == TEXT("game_mode_base") || Family == TEXT("game_mode") || Family == TEXT("game_instance");
        TestEqual(*FString::Printf(TEXT("%s assignment capability is exact"), *Family),
            Operations->GetBoolField(TEXT("project_settings_assignment")), bAssignable);
        TestTrue(*FString::Printf(TEXT("%s publishes multiplayer detail"), *Family),
            Record->HasTypedField<EJson::Object>(TEXT("multiplayer")));
    }

    const FString PackageName = TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_MultiplayerCharacter");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, ACharacter::StaticClass(), false);
    if (!TestNotNull(TEXT("multiplayer Character fixture creates"), Blueprint)) return false;
    const FString AssetPath = Blueprint->GetPathName();
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    FString Snapshot = InspectSnapshot(Inspector, AssetPath);

    auto SetReplication = [&](const TCHAR* Setting, const TSharedPtr<FJsonValue>& Value)
    {
        const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
        Arguments->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
        Arguments->SetStringField(TEXT("asset_path"), AssetPath);
        Arguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
        Arguments->SetStringField(TEXT("replication_setting"), Setting);
        Arguments->SetField(TEXT("value"), Value);
        const bool bOk = Mutator.Execute(TEXT("blueprint_default_edit"), Arguments, Result, Error);
        if (bOk) Snapshot = Result->GetStringField(TEXT("snapshot_id"));
        return bOk;
    };
    TestTrue(TEXT("Actor replication enables"), SetReplication(TEXT("replicates"), MakeShared<FJsonValueBoolean>(true)));
    TestTrue(TEXT("movement replication enables after Actor replication"),
        SetReplication(TEXT("replicate_movement"), MakeShared<FJsonValueBoolean>(true)));
    TestTrue(TEXT("net priority sets through typed path"),
        SetReplication(TEXT("net_priority"), MakeShared<FJsonValueNumber>(2.5)));

    TSharedRef<FJsonObject> AddComponent = ComponentEditArguments(AssetPath, Snapshot, TEXT("add"));
    AddComponent->SetStringField(TEXT("component_class"), UStaticMeshComponent::StaticClass()->GetPathName());
    AddComponent->SetStringField(TEXT("name"), TEXT("ReplicatedMesh"));
    if (!TestTrue(TEXT("replicated component fixture adds"),
        Mutator.Execute(TEXT("blueprint_component_edit"), AddComponent, Result, Error))) return false;
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    const FString ComponentId = Result->GetObjectField(TEXT("changed"))->GetStringField(TEXT("component_id"));
    TSharedRef<FJsonObject> SetComponent = ComponentEditArguments(AssetPath, Snapshot, TEXT("set_replication"));
    SetComponent->SetStringField(TEXT("component_id"), ComponentId);
    SetComponent->SetBoolField(TEXT("replicates"), true);
    if (!TestTrue(TEXT("component replication enables after Actor replication"),
        Mutator.Execute(TEXT("blueprint_component_edit"), SetComponent, Result, Error))) return false;
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    const FString GraphId = EventGraphId(Blueprint);
    auto AddRpc = [&](const TCHAR* Name, const TCHAR* Mode, const TCHAR* Reliability, FString& OutId)
    {
        TSharedRef<FJsonObject> Arguments = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("custom_event"), TEXT("add"));
        Arguments->SetStringField(TEXT("graph_id"), GraphId);
        Arguments->SetStringField(TEXT("name"), Name);
        Arguments->SetObjectField(TEXT("signature"), EmptyEventSignature());
        const TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
        Metadata->SetStringField(TEXT("rpc_mode"), Mode);
        Metadata->SetStringField(TEXT("reliability"), Reliability);
        Arguments->SetObjectField(TEXT("metadata"), Metadata);
        const bool bOk = Mutator.Execute(TEXT("blueprint_member_edit"), Arguments, Result, Error);
        if (bOk)
        {
            Snapshot = Result->GetStringField(TEXT("snapshot_id"));
            OutId = Result->GetObjectField(TEXT("custom_event"))->GetStringField(TEXT("id"));
        }
        return bOk;
    };
    FString ServerId;
    FString ClientId;
    FString MulticastId;
    TestTrue(TEXT("reliable server RPC adds"), AddRpc(TEXT("ServerFire"), TEXT("server"), TEXT("reliable"), ServerId));
    TestTrue(TEXT("unreliable owning-client RPC adds"), AddRpc(TEXT("ClientConfirmFire"), TEXT("client"), TEXT("unreliable"), ClientId));
    TestTrue(TEXT("reliable multicast RPC adds"), AddRpc(TEXT("MulticastPlayFire"), TEXT("multicast"), TEXT("reliable"), MulticastId));

    TSharedRef<FJsonObject> Invalid = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("custom_event"), TEXT("add"));
    Invalid->SetStringField(TEXT("graph_id"), GraphId);
    Invalid->SetStringField(TEXT("name"), TEXT("InvalidReliableLocal"));
    Invalid->SetObjectField(TEXT("signature"), EmptyEventSignature());
    const TSharedRef<FJsonObject> InvalidMetadata = MakeShared<FJsonObject>();
    InvalidMetadata->SetStringField(TEXT("rpc_mode"), TEXT("not_replicated"));
    InvalidMetadata->SetStringField(TEXT("reliability"), TEXT("reliable"));
    Invalid->SetObjectField(TEXT("metadata"), InvalidMetadata);
    TestFalse(TEXT("reliable non-RPC rejects before mutation"),
        Mutator.Execute(TEXT("blueprint_member_edit"), Invalid, Result, Error));
    TestEqual(TEXT("invalid reliability preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> Compile = AssetArguments(AssetPath);
    Compile->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Compile->SetStringField(TEXT("expected_snapshot"), Snapshot);
    if (!TestTrue(TEXT("multiplayer Blueprint compiles"), Mutator.Execute(TEXT("blueprint_compile"), Compile, Result, Error))) return false;
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TestTrue(TEXT("multiplayer compile succeeds"), Result->GetBoolField(TEXT("compile_succeeded")));
    UFunction* Server = Blueprint->GeneratedClass->FindFunctionByName(TEXT("ServerFire"));
    UFunction* Client = Blueprint->GeneratedClass->FindFunctionByName(TEXT("ClientConfirmFire"));
    UFunction* Multicast = Blueprint->GeneratedClass->FindFunctionByName(TEXT("MulticastPlayFire"));
    TestTrue(TEXT("server function flags survive compile"), Server != nullptr
        && Server->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));
    TestTrue(TEXT("client function flags survive compile"), Client != nullptr
        && Client->HasAllFunctionFlags(FUNC_Net | FUNC_NetClient) && !Client->HasAnyFunctionFlags(FUNC_NetReliable));
    TestTrue(TEXT("multicast function flags survive compile"), Multicast != nullptr
        && Multicast->HasAllFunctionFlags(FUNC_Net | FUNC_NetMulticast | FUNC_NetReliable));

    TSharedRef<FJsonObject> Inspect = InspectArguments(AssetPath);
    Inspect->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("components")),
        MakeShared<FJsonValueString>(TEXT("class_defaults")), MakeShared<FJsonValueString>(TEXT("custom_events"))});
    Inspect->SetArrayField(TEXT("property_names"), {MakeShared<FJsonValueString>(TEXT("bReplicates")),
        MakeShared<FJsonValueString>(TEXT("bReplicateMovement")), MakeShared<FJsonValueString>(TEXT("NetPriority"))});
    if (!TestTrue(TEXT("multiplayer contracts inspect"), Inspector.Execute(Inspect, Result, Error))) return false;
    int32 RpcRecords = 0;
    for (const TSharedPtr<FJsonValue>& Value : Result->GetArrayField(TEXT("records")))
    {
        const TSharedPtr<FJsonObject> Record = Value->AsObject();
        if (Record.IsValid() && Record->GetStringField(TEXT("section")) == TEXT("custom_event")
            && Record->GetObjectField(TEXT("metadata"))->GetStringField(TEXT("rpc_mode")) != TEXT("not_replicated")) ++RpcRecords;
    }
    TestEqual(TEXT("three authored RPCs inspect exactly"), RpcRecords, 3);
    TestTrue(TEXT("live multiplayer capabilities are published"),
        Result->GetObjectField(TEXT("family_capabilities"))->HasTypedField<EJson::Object>(TEXT("multiplayer")));

    TSharedRef<FJsonObject> Save = AssetArguments(AssetPath);
    Save->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Save->SetStringField(TEXT("expected_snapshot"), Snapshot);
    TestTrue(TEXT("multiplayer Blueprint saves"), Mutator.Execute(TEXT("blueprint_save"), Save, Result, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase16FrameworkAssignmentTest,
    "UnrealMCP.Phase16.FrameworkAssignment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase16FrameworkAssignmentTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString Prefix = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UBlueprint* GameMode = CreateBlueprintFixture(Prefix + TEXT("/BP_DefaultGameMode"), AGameModeBase::StaticClass(), false);
    UBlueprint* GameInstance = CreateBlueprintFixture(Prefix + TEXT("/BP_DefaultGameInstance"), UGameInstance::StaticClass(), false);
    if (!TestNotNull(TEXT("GameMode assignment fixture creates"), GameMode)
        || !TestNotNull(TEXT("GameInstance assignment fixture creates"), GameInstance)
        || !TestTrue(TEXT("GameMode assignment fixture saves"), SaveBlueprintFixture(GameMode))
        || !TestTrue(TEXT("GameInstance assignment fixture saves"), SaveBlueprintFixture(GameInstance))) return false;

    const FString ProjectHash = FString::ChrN(40, TEXT('a'));
    FUnrealMCPGameplayFrameworkEditor Editor(ProjectHash);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    const FString OldGameMode = UGameMapsSettings::GetGlobalDefaultGameMode();
    const FString OldGameInstance = GetDefault<UGameMapsSettings>()->GameInstanceClass.ToString();
    if (!TestTrue(TEXT("existing GameMode setting is restorable"), !OldGameMode.IsEmpty())
        || !TestTrue(TEXT("existing GameInstance setting is restorable"), !OldGameInstance.IsEmpty())) return false;

    TestFalse(TEXT("stale framework assignment rejects"), Editor.Execute(FrameworkArguments(
        ProjectHash, TEXT("default_game_mode"), GameMode->GeneratedClass->GetPathName(), TEXT("/Script/Engine.GameMode")), Result, Error));
    TestEqual(TEXT("stale framework assignment has stable code"), Error.Code, FString(TEXT("stale_precondition")));
    TestFalse(TEXT("wrong-family framework assignment rejects"), Editor.Execute(FrameworkArguments(
        ProjectHash, TEXT("default_game_mode"), GameInstance->GeneratedClass->GetPathName(), OldGameMode), Result, Error));
    TestEqual(TEXT("wrong family has stable code"), Error.Code, FString(TEXT("invalid_parent")));

    if (!TestTrue(TEXT("saved Blueprint GameMode assigns"), Editor.Execute(FrameworkArguments(
        ProjectHash, TEXT("default_game_mode"), GameMode->GeneratedClass->GetPathName(), OldGameMode), Result, Error))) return false;
    TestEqual(TEXT("GameMode assignment reads back"), UGameMapsSettings::GetGlobalDefaultGameMode(), GameMode->GeneratedClass->GetPathName());
    TestFalse(TEXT("framework assignment does not require restart"), Result->GetBoolField(TEXT("restart_required")));
    if (!TestTrue(TEXT("native GameMode restores"), Editor.Execute(FrameworkArguments(
        ProjectHash, TEXT("default_game_mode"), OldGameMode, GameMode->GeneratedClass->GetPathName()), Result, Error))) return false;

    if (!TestTrue(TEXT("saved Blueprint GameInstance assigns"), Editor.Execute(FrameworkArguments(
        ProjectHash, TEXT("default_game_instance"), GameInstance->GeneratedClass->GetPathName(), OldGameInstance), Result, Error))) return false;
    TestEqual(TEXT("GameInstance assignment reads back"), GetDefault<UGameMapsSettings>()->GameInstanceClass.ToString(),
        GameInstance->GeneratedClass->GetPathName());
    if (!TestTrue(TEXT("native GameInstance restores"), Editor.Execute(FrameworkArguments(
        ProjectHash, TEXT("default_game_instance"), OldGameInstance, GameInstance->GeneratedClass->GetPathName()), Result, Error))) return false;
    TestEqual(TEXT("GameMode setting restored"), UGameMapsSettings::GetGlobalDefaultGameMode(), OldGameMode);
    TestEqual(TEXT("GameInstance setting restored"), GetDefault<UGameMapsSettings>()->GameInstanceClass.ToString(), OldGameInstance);
    return true;
}

#endif
