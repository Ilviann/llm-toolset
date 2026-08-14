#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

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
    WidgetTree,
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

UNREALMCPBLUEPRINT_API FFamilyInfo Classify(const UClass* Class);
UNREALMCPBLUEPRINT_API bool Supports(const UClass* Class, EOperation Operation);
UNREALMCPBLUEPRINT_API bool SupportsActorReplication(const UClass* Class);
UNREALMCPBLUEPRINT_API bool SupportsComponentReplication(const UClass* Class);
UNREALMCPBLUEPRINT_API bool SupportsReplicatedVariables(const UClass* Class);
UNREALMCPBLUEPRINT_API bool SupportsRpcMode(const UClass* Class, const FString& Mode);
UNREALMCPBLUEPRINT_API TSharedRef<FUnrealMCPRecord> BuildLiveCapabilities(const UBlueprint* Blueprint);
UNREALMCPBLUEPRINT_API TSharedRef<FUnrealMCPRecord> BuildLiveCapabilities(
    const UBlueprint* Blueprint,
    const FFamilyInfo& Family);
UNREALMCPBLUEPRINT_API TArray<TSharedPtr<FUnrealMCPValue>> BuildPublishedMatrix();
}
