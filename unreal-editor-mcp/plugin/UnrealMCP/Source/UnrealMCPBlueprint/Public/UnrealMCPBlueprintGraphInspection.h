#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class FUnrealMCPAssetFamilyDocumentBuilder;
class FUnrealMCPRecord;
class UEdGraph;
class UEdGraphNode;
struct FUnrealMCPAssetFamilyInspectionContext;
struct FUnrealMCPError;

namespace UnrealMCP::BlueprintGraphInspection
{
struct FSelection
{
    UEdGraph* Graph = nullptr;
    UEdGraphNode* PreferredRoot = nullptr;
    FString Kind;
    FString Name;
    FString Selector;
    bool bTraverseInputsFromRoot = false;
    TFunction<void(UEdGraphNode*, const TSharedRef<FUnrealMCPRecord>&)> DecorateNode;
};

UNREALMCPBLUEPRINT_API bool InspectGraph(
    const FUnrealMCPAssetFamilyInspectionContext& Context,
    const FSelection& Selection,
    FUnrealMCPAssetFamilyDocumentBuilder& Document,
    FUnrealMCPError& OutError);
}
