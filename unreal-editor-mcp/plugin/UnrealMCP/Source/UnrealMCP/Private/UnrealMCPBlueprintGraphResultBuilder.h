#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPBlueprintGraphRequestValidation.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;

namespace UnrealMCP::BlueprintGraphResultBuilder
{
TSharedRef<FJsonObject> EncodeNode(UEdGraph* Graph, UEdGraphNode* Node);

TSharedRef<FJsonObject> Build(
    UBlueprint* Blueprint,
    const BlueprintGraphRequestValidation::FRequest& Request,
    const FString& Snapshot,
    const TSharedRef<FJsonObject>& Changed,
    const TSet<FString>& CreatedIdentities,
    const TSet<FString>& ReconstructedIdentities);
}
