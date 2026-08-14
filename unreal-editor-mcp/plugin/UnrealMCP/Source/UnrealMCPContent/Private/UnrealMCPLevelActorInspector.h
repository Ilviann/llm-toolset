#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class UWorld;

class FUnrealMCPLevelActorInspector
{
public:
    static bool BuildRecords(
        const FUnrealMCPRecord& Arguments,
        UWorld* World,
        const FString& MapId,
        const FString& SnapshotId,
        TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
        bool& OutScanTruncated,
        FUnrealMCPError& OutError);
};
