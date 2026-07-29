#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPBlueprintActionCatalog.h"
#include "UnrealMCPProtocol.h"

class FCompilerResultsLog;
class FUnrealMCPBlueprintInspector;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UEdGraphSchema_K2;

class FUnrealMCPBlueprintBlockReplacementService
{
public:
    using FCompile = TFunction<void(UBlueprint*, FCompilerResultsLog&)>;
    using FNodeInvoker = TFunction<UEdGraphNode*(
        const FUnrealMCPBlueprintActionCatalog::FResolvedAction&, UEdGraph*, const FVector2D&)>;
    using FConnectionInvoker =
        TFunction<bool(const UEdGraphSchema_K2*, UEdGraphPin*, UEdGraphPin*)>;

    FUnrealMCPBlueprintBlockReplacementService(
        FUnrealMCPBlueprintInspector& InInspector,
        FUnrealMCPBlueprintActionCatalog& InActionCatalog,
        FCompile InCompile = {},
        FNodeInvoker InNodeInvoker = {},
        FConnectionInvoker InConnectionInvoker = {});

    bool Execute(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

private:
    FUnrealMCPBlueprintInspector& Inspector;
    FUnrealMCPBlueprintActionCatalog& ActionCatalog;
    FCompile CompileBlueprint;
    FNodeInvoker NodeInvoker;
    FConnectionInvoker ConnectionInvoker;
};
