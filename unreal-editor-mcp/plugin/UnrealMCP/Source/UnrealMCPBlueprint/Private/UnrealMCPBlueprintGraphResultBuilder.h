#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "UnrealMCPBlueprintGraphRequestValidation.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

namespace UnrealMCP::BlueprintGraphResultBuilder
{
TSharedRef<FUnrealMCPRecord> EncodeNode(UEdGraph* Graph, UEdGraphNode* Node);

TSharedRef<FUnrealMCPRecord> Build(
    UBlueprint* Blueprint,
    const BlueprintGraphRequestValidation::FRequest& Request,
    const FString& Snapshot,
    const TSharedRef<FUnrealMCPRecord>& Changed,
    const TSet<FString>& CreatedIdentities,
    const TSet<FString>& ReconstructedIdentities);
}
