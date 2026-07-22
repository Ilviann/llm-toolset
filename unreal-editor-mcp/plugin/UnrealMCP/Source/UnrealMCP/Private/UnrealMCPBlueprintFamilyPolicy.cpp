#include "UnrealMCPBlueprintFamilyPolicy.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameState.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"

namespace UnrealMCP::BlueprintFamilyPolicy
{
namespace
{
FFamilyInfo MakeFamily(const TCHAR* Name, const UClass* NativeBase)
{
    return {Name, NativeBase != nullptr ? NativeBase->GetPathName() : FString(), true};
}

TSharedRef<FJsonObject> PublishedOperations()
{
    const TSharedRef<FJsonObject> Operations = MakeShared<FJsonObject>();
    for (const TCHAR* Name : {TEXT("discover"), TEXT("inspect"), TEXT("create"), TEXT("compile"), TEXT("save"),
        TEXT("class_defaults"), TEXT("components"), TEXT("member_variables"), TEXT("functions"),
        TEXT("local_variables"), TEXT("macros"), TEXT("custom_events"), TEXT("action_catalog"), TEXT("graph_edit")})
    {
        Operations->SetBoolField(Name, true);
    }
    Operations->SetBoolField(TEXT("parent_change"), false);
    Operations->SetBoolField(TEXT("project_settings_assignment"), false);
    return Operations;
}
}

FFamilyInfo Classify(const UClass* Class)
{
    if (Class == nullptr || !Class->IsChildOf(AActor::StaticClass()))
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
    case EOperation::Components:
    case EOperation::Members:
    case EOperation::ActionCatalog:
    case EOperation::GraphEdit:
        return true;
    default:
        return false;
    }
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
    Result->SetBoolField(TEXT("components"), Family.bSupported && bComponents);
    Result->SetBoolField(TEXT("event_graphs"), Family.bSupported && bEventGraph);
    Result->SetBoolField(TEXT("local_variables"), Family.bSupported && bNormalBlueprint);
    Result->SetBoolField(TEXT("overrides"), Family.bSupported && bOverrides);
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
        MakeFamily(TEXT("game_state"), AGameState::StaticClass())};
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const FFamilyInfo& Family : Families)
    {
        const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("family"), Family.Name);
        Record->SetStringField(TEXT("native_base_class"), Family.NativeBaseClass);
        Record->SetStringField(TEXT("inheritance_category"), TEXT("actor_derived"));
        Record->SetObjectField(TEXT("operations"), PublishedOperations());
        Result.Add(MakeShared<FJsonValueObject>(Record));
    }
    return Result;
}
}
