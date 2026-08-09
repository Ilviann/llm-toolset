#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPBlueprintBlockReplacementRequest.h"
#include "UnrealMCPProtocol.h"

class UEdGraph;
class UEdGraphNode;

namespace UnrealMCP::BlueprintNodeLayout
{
using UnrealMCP::BlueprintBlockReplacement::FPosition;

struct FResult
{
    TMap<FString, FPosition> Positions;
    FString Fingerprint;
    int32 Iterations = 0;
    int32 MinX = 0;
    int32 MinY = 0;
    int32 MaxX = 0;
    int32 MaxY = 0;
};

bool PlanAndApply(
    UEdGraph* Graph,
    const TMap<FString, UEdGraphNode*>& NodesByKey,
    FResult& OutResult,
    FUnrealMCPError& OutError);

bool ApplyResolved(
    const TMap<FString, UEdGraphNode*>& NodesByKey,
    const FResult& Result,
    FUnrealMCPError& OutError);
}
