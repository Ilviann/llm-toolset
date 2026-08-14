#include "UnrealMCPBlueprintGraphEditor.h"

#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"

FUnrealMCPBlueprintGraphEditor::FUnrealMCPBlueprintGraphEditor(
    FUnrealMCPBlueprintInspector& InInspector,
    FUnrealMCPBlueprintActionCatalog& InActionCatalog,
    FActionResolver InActionResolver,
    FNodeInvoker InNodeInvoker,
    FConnectionInvoker InConnectionInvoker)
    : Inspector(InInspector), ActionCatalog(InActionCatalog), ActionResolver(MoveTemp(InActionResolver)),
      NodeInvoker(MoveTemp(InNodeInvoker)), ConnectionInvoker(MoveTemp(InConnectionInvoker))
{
    if (!ActionResolver)
    {
        ActionResolver = [this](const FString& ActionId, UBlueprint* Blueprint, UEdGraph* Graph,
            const FString& AssetPath, const FString& GraphId, const FString& SnapshotId,
            FUnrealMCPBlueprintActionCatalog::FResolvedAction& OutAction, FUnrealMCPError& OutError)
        {
            return ActionCatalog.ResolveForInvocation(ActionId, Blueprint, Graph, AssetPath, GraphId, SnapshotId, OutAction, OutError);
        };
    }
    if (!NodeInvoker)
    {
        NodeInvoker = [](const FUnrealMCPBlueprintActionCatalog::FResolvedAction& Action, UEdGraph* Graph, const FVector2D& Position)
        {
            return Action.Spawner != nullptr ? Action.Spawner->Invoke(Graph, Action.Bindings, Position) : nullptr;
        };
    }
    if (!ConnectionInvoker)
    {
        ConnectionInvoker = [](const UEdGraphSchema_K2* Schema, UEdGraphPin* FromPin, UEdGraphPin* ToPin)
        {
            return Schema != nullptr && Schema->TryCreateConnection(FromPin, ToPin);
        };
    }
}
