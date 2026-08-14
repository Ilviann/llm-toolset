#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "UnrealMCPBlueprintGraphEditor.h"
#include "UnrealMCPBlueprintGraphRequestValidation.h"

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
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError);
}
