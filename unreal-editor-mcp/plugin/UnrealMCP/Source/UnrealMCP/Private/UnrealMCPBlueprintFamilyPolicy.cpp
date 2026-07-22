#include "UnrealMCPBlueprintFamilyPolicy.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/GameInstance.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameState.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UnrealMCPPropertyCodec.h"
#include "UObject/UnrealType.h"

namespace UnrealMCP::BlueprintFamilyPolicy
{
namespace
{
FFamilyInfo MakeFamily(const TCHAR* Name, const UClass* NativeBase)
{
    return {Name, NativeBase != nullptr ? NativeBase->GetPathName() : FString(), true};
}

TArray<TSharedPtr<FJsonValue>> StringValues(std::initializer_list<const TCHAR*> Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const TCHAR* Value : Values) Result.Add(MakeShared<FJsonValueString>(Value));
    return Result;
}

TSharedRef<FJsonObject> PublishedOperations(const FFamilyInfo& Family)
{
    const TSharedRef<FJsonObject> Operations = MakeShared<FJsonObject>();
    for (const TCHAR* Name : {TEXT("discover"), TEXT("inspect"), TEXT("create"), TEXT("compile"), TEXT("save"),
        TEXT("class_defaults"), TEXT("member_variables"), TEXT("functions"),
        TEXT("local_variables"), TEXT("macros"), TEXT("custom_events"), TEXT("action_catalog"), TEXT("graph_edit")})
    {
        Operations->SetBoolField(Name, true);
    }
    Operations->SetBoolField(TEXT("components"), Family.Name != TEXT("game_instance"));
    Operations->SetBoolField(TEXT("parent_change"), false);
    Operations->SetBoolField(TEXT("project_settings_assignment"), Family.Name == TEXT("game_mode_base")
        || Family.Name == TEXT("game_mode") || Family.Name == TEXT("game_instance"));
    return Operations;
}

TSharedRef<FJsonObject> PublishedMultiplayer(const FFamilyInfo& Family)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    const bool bActorReplication = Family.Name == TEXT("actor") || Family.Name == TEXT("game_state_base")
        || Family.Name == TEXT("game_state");
    Result->SetBoolField(TEXT("actor_replication"), bActorReplication);
    Result->SetBoolField(TEXT("component_replication"), bActorReplication);
    Result->SetBoolField(TEXT("replicated_variables"), bActorReplication);
    if (Family.Name == TEXT("actor"))
        Result->SetArrayField(TEXT("rpc_modes"), StringValues({TEXT("not_replicated"), TEXT("server"), TEXT("client"), TEXT("multicast")}));
    else if (Family.Name == TEXT("game_mode_base") || Family.Name == TEXT("game_mode"))
        Result->SetArrayField(TEXT("rpc_modes"), StringValues({TEXT("not_replicated"), TEXT("server")}));
    else if (Family.Name == TEXT("game_state_base") || Family.Name == TEXT("game_state"))
        Result->SetArrayField(TEXT("rpc_modes"), StringValues({TEXT("not_replicated"), TEXT("multicast")}));
    else
        Result->SetArrayField(TEXT("rpc_modes"), StringValues({TEXT("not_replicated")}));
    return Result;
}
}

FFamilyInfo Classify(const UClass* Class)
{
    if (Class == nullptr)
    {
        return {};
    }
    if (Class->IsChildOf(UGameInstance::StaticClass()))
    {
        return MakeFamily(TEXT("game_instance"), UGameInstance::StaticClass());
    }
    if (!Class->IsChildOf(AActor::StaticClass()))
    {
        return {};
    }
    if (Class->IsChildOf(AGameMode::StaticClass()))
    {
        return MakeFamily(TEXT("game_mode"), AGameMode::StaticClass());
    }
    if (Class->IsChildOf(AGameModeBase::StaticClass()))
    {
        return MakeFamily(TEXT("game_mode_base"), AGameModeBase::StaticClass());
    }
    if (Class->IsChildOf(AGameState::StaticClass()))
    {
        return MakeFamily(TEXT("game_state"), AGameState::StaticClass());
    }
    if (Class->IsChildOf(AGameStateBase::StaticClass()))
    {
        return MakeFamily(TEXT("game_state_base"), AGameStateBase::StaticClass());
    }
    return MakeFamily(TEXT("actor"), AActor::StaticClass());
}

bool Supports(const UClass* Class, EOperation Operation)
{
    const FFamilyInfo Family = Classify(Class);
    if (!Family.bSupported)
    {
        return false;
    }
    switch (Operation)
    {
    case EOperation::Discover:
    case EOperation::Inspect:
    case EOperation::Create:
    case EOperation::Compile:
    case EOperation::Save:
    case EOperation::ClassDefaults:
    case EOperation::Members:
    case EOperation::ActionCatalog:
    case EOperation::GraphEdit:
        return true;
    case EOperation::Components:
        return Family.Name != TEXT("game_instance");
    default:
        return false;
    }
}

bool SupportsActorReplication(const UClass* Class)
{
    const FString Family = Classify(Class).Name;
    return Family == TEXT("actor") || Family == TEXT("game_state_base") || Family == TEXT("game_state");
}

bool SupportsComponentReplication(const UClass* Class)
{
    return SupportsActorReplication(Class);
}

bool SupportsReplicatedVariables(const UClass* Class)
{
    return SupportsActorReplication(Class);
}

bool SupportsRpcMode(const UClass* Class, const FString& Mode)
{
    if (Mode == TEXT("not_replicated")) return Classify(Class).bSupported;
    const FString Family = Classify(Class).Name;
    if (Family == TEXT("actor")) return Mode == TEXT("server") || Mode == TEXT("client") || Mode == TEXT("multicast");
    if (Family == TEXT("game_mode_base") || Family == TEXT("game_mode")) return Mode == TEXT("server");
    if (Family == TEXT("game_state_base") || Family == TEXT("game_state")) return Mode == TEXT("multicast");
    return false;
}

TSharedRef<FJsonObject> BuildLiveCapabilities(const UBlueprint* Blueprint)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    const UClass* ParentClass = Blueprint != nullptr ? Blueprint->ParentClass : nullptr;
    const FFamilyInfo Family = Classify(ParentClass);
    const bool bNormalBlueprint = Blueprint != nullptr && Blueprint->BlueprintType == BPTYPE_Normal;
    const bool bDefaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
        && Blueprint->GeneratedClass->GetDefaultObject(false) != nullptr;
    const bool bComponents = Blueprint != nullptr && Blueprint->SimpleConstructionScript != nullptr;
    bool bEventGraph = false;
    if (Blueprint != nullptr)
    {
        for (const UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (Graph != nullptr && FBlueprintEditorUtils::IsEventGraph(Graph)
                && Graph->GetSchema() != nullptr && Graph->GetSchema()->IsA<UEdGraphSchema_K2>())
            {
                bEventGraph = true;
                break;
            }
        }
    }
    bool bOverrides = false;
    if (ParentClass != nullptr)
    {
        for (TFieldIterator<UFunction> It(ParentClass, EFieldIterationFlags::IncludeSuper); It; ++It)
        {
            if (It->HasAnyFunctionFlags(FUNC_BlueprintEvent))
            {
                bOverrides = true;
                break;
            }
        }
    }

    Result->SetBoolField(TEXT("class_defaults"), Family.bSupported && bDefaults);
    Result->SetBoolField(TEXT("components"), Supports(ParentClass, EOperation::Components) && bComponents);
    Result->SetBoolField(TEXT("event_graphs"), Family.bSupported && bEventGraph);
    Result->SetBoolField(TEXT("local_variables"), Family.bSupported && bNormalBlueprint);
    Result->SetBoolField(TEXT("overrides"), Family.bSupported && bOverrides);
    const TSharedRef<FJsonObject> Multiplayer = PublishedMultiplayer(Family);
    TArray<TSharedPtr<FJsonValue>> Settings;
    UObject* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
        ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
    if (Defaults != nullptr && SupportsActorReplication(ParentClass))
    {
        for (const TPair<const TCHAR*, const TCHAR*>& Entry : {
            TPair<const TCHAR*, const TCHAR*>(TEXT("replicates"), TEXT("bReplicates")),
            {TEXT("replicate_movement"), TEXT("bReplicateMovement")}, {TEXT("always_relevant"), TEXT("bAlwaysRelevant")},
            {TEXT("only_relevant_to_owner"), TEXT("bOnlyRelevantToOwner")}, {TEXT("use_owner_relevancy"), TEXT("bNetUseOwnerRelevancy")},
            {TEXT("dormancy"), TEXT("NetDormancy")}, {TEXT("net_priority"), TEXT("NetPriority")},
            {TEXT("net_update_frequency"), TEXT("NetUpdateFrequency")}, {TEXT("min_net_update_frequency"), TEXT("MinNetUpdateFrequency")}})
        {
            FString Kind;
            if (UnrealMCP::PropertyCodec::IsSupportedEditable(Defaults->GetClass()->FindPropertyByName(Entry.Value), Kind))
                Settings.Add(MakeShared<FJsonValueString>(Entry.Key));
        }
    }
    Multiplayer->SetArrayField(TEXT("actor_replication_settings"), Settings);
    Result->SetObjectField(TEXT("multiplayer"), Multiplayer);
    const TSharedRef<FJsonObject> GraphTypes = MakeShared<FJsonObject>();
    GraphTypes->SetBoolField(TEXT("event"), Family.bSupported && bEventGraph);
    GraphTypes->SetBoolField(TEXT("function"), Family.bSupported && bNormalBlueprint);
    GraphTypes->SetBoolField(TEXT("macro"), Family.bSupported && bNormalBlueprint);
    Result->SetObjectField(TEXT("graph_types"), GraphTypes);
    return Result;
}

TArray<TSharedPtr<FJsonValue>> BuildPublishedMatrix()
{
    const TArray<FFamilyInfo> Families = {
        MakeFamily(TEXT("actor"), AActor::StaticClass()),
        MakeFamily(TEXT("game_mode_base"), AGameModeBase::StaticClass()),
        MakeFamily(TEXT("game_mode"), AGameMode::StaticClass()),
        MakeFamily(TEXT("game_state_base"), AGameStateBase::StaticClass()),
        MakeFamily(TEXT("game_state"), AGameState::StaticClass()),
        MakeFamily(TEXT("game_instance"), UGameInstance::StaticClass())};
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const FFamilyInfo& Family : Families)
    {
        const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("family"), Family.Name);
        Record->SetStringField(TEXT("native_base_class"), Family.NativeBaseClass);
        Record->SetStringField(TEXT("inheritance_category"),
            Family.Name == TEXT("game_instance") ? TEXT("uobject_derived") : TEXT("actor_derived"));
        Record->SetObjectField(TEXT("operations"), PublishedOperations(Family));
        Record->SetObjectField(TEXT("multiplayer"), PublishedMultiplayer(Family));
        Result.Add(MakeShared<FJsonValueObject>(Record));
    }
    return Result;
}
}
