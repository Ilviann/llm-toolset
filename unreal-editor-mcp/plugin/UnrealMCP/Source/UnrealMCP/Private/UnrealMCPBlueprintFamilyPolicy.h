#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;
class UClass;

namespace UnrealMCP::BlueprintFamilyPolicy
{
enum class EOperation : uint8
{
    Discover,
    Inspect,
    Create,
    Compile,
    Save,
    ClassDefaults,
    Components,
    Members,
    ActionCatalog,
    GraphEdit
};

struct FFamilyInfo
{
    FString Name;
    FString NativeBaseClass;
    bool bSupported = false;
};

FFamilyInfo Classify(const UClass* Class);
bool Supports(const UClass* Class, EOperation Operation);
bool SupportsActorReplication(const UClass* Class);
bool SupportsComponentReplication(const UClass* Class);
bool SupportsReplicatedVariables(const UClass* Class);
bool SupportsRpcMode(const UClass* Class, const FString& Mode);
TSharedRef<FJsonObject> BuildLiveCapabilities(const UBlueprint* Blueprint);
TArray<TSharedPtr<FJsonValue>> BuildPublishedMatrix();
}
