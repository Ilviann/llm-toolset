#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;

namespace UnrealMCP::BlueprintLogicUnitFingerprint
{
enum class EKind : uint8
{
    Function,
    Macro,
    CustomEvent,
    Event,
};

struct FExternalLink
{
    FString FromNodeId;
    FString FromPinId;
    FString ToNodeId;
    FString ToPinId;
};

struct FBoundary
{
    EKind Kind = EKind::Function;
    UEdGraph* Graph = nullptr;
    UEdGraphNode* Entry = nullptr;
    UEdGraphNode* Result = nullptr;
    TArray<UEdGraphNode*> OwnedNodes;
    TArray<FString> OwnedNodeIds;
    TArray<FString> LocalVariableIds;
    TArray<FExternalLink> ExternalLinks;
    FString Fingerprint;
};

FString KindString(EKind Kind);
FString Build(const FBoundary& Boundary);
bool DescribeFunction(UEdGraph* Graph, FBoundary& OutBoundary);
bool DescribeMacro(UEdGraph* Graph, FBoundary& OutBoundary);
bool DescribeEventHandler(UEdGraph* Graph, UEdGraphNode* EventRoot, FBoundary& OutBoundary);
}
