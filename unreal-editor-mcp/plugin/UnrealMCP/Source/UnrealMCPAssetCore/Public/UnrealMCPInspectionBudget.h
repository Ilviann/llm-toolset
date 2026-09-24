#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
struct FUnrealMCPError;

namespace UnrealMCP::InspectionBudget
{
// Internal base-domain helpers; these do not change the companion protocol.
UNREALMCPASSETCORE_API bool Check(int32 Records, int32 Fingerprints, FUnrealMCPError& OutError);
UNREALMCPASSETCORE_API bool CollectGraphs(
    UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs, int32& InOutWork,
    FUnrealMCPError& OutError);
}
