#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPBlueprintActionCatalog.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPBlueprintInspector;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;

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

    FUnrealMCPBlueprintGraphEditor(
        FUnrealMCPBlueprintInspector& InInspector,
        FUnrealMCPBlueprintActionCatalog& InActionCatalog,
        FActionResolver InActionResolver = {},
        FNodeInvoker InNodeInvoker = {});

    bool Execute(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

private:
    FUnrealMCPBlueprintInspector& Inspector;
    FUnrealMCPBlueprintActionCatalog& ActionCatalog;
    FActionResolver ActionResolver;
    FNodeInvoker NodeInvoker;
};
