#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;
class UK2Node_FunctionEntry;
class UK2Node_FunctionResult;

namespace UnrealMCP::BlueprintFunctionFingerprint
{
struct FBoundary
{
    UK2Node_FunctionEntry* Entry = nullptr;
    UK2Node_FunctionResult* Result = nullptr;
    TArray<UEdGraphNode*> OwnedNodes;
    TArray<FString> OwnedNodeIds;
    TArray<FString> LocalVariableIds;
    FString Fingerprint;
};

FString Build(UEdGraph* Graph);
bool Describe(UEdGraph* Graph, FBoundary& OutBoundary);
}
