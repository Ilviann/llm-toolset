#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPBlueprintGraphEditor.h"
#include "UnrealMCPBlueprintGraphRequestValidation.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector;
class UBlueprint;
class UEdGraph;

namespace UnrealMCP::BlueprintGraphPinOperationHandler
{
bool Execute(
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    const BlueprintGraphRequestValidation::FRequest& Request,
    FUnrealMCPBlueprintInspector& Inspector,
    const FUnrealMCPBlueprintGraphEditor::FConnectionInvoker& ConnectionInvoker,
    const FString& Snapshot,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError);
}
