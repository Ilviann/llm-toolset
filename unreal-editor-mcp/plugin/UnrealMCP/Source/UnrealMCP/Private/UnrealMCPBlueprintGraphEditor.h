#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPBlueprintActionCatalog.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UEdGraphSchema_K2;

class FUnrealMCPBlueprintGraphEditor
{
public:
    using FActionResolver = TFunction<bool(
        const FString&,
        UBlueprint*,
        UEdGraph*,
        const FString&,
        const FString&,
        const FString&,
        FUnrealMCPBlueprintActionCatalog::FResolvedAction&,
        FUnrealMCPError&)>;
    using FNodeInvoker = TFunction<UEdGraphNode*(
        const FUnrealMCPBlueprintActionCatalog::FResolvedAction&,
        UEdGraph*,
        const FVector2D&)>;
    using FConnectionInvoker = TFunction<bool(const UEdGraphSchema_K2*, UEdGraphPin*, UEdGraphPin*)>;

    FUnrealMCPBlueprintGraphEditor(
        FUnrealMCPBlueprintInspector& InInspector,
        FUnrealMCPBlueprintActionCatalog& InActionCatalog,
        FActionResolver InActionResolver = {},
        FNodeInvoker InNodeInvoker = {},
        FConnectionInvoker InConnectionInvoker = {});

    bool Execute(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

private:
    FUnrealMCPBlueprintInspector& Inspector;
    FUnrealMCPBlueprintActionCatalog& ActionCatalog;
    FActionResolver ActionResolver;
    FNodeInvoker NodeInvoker;
    FConnectionInvoker ConnectionInvoker;
};
