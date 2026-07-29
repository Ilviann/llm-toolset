#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class UWorld;

class FUnrealMCPLevelActorInspector
{
public:
    static bool BuildRecords(
        const FJsonObject& Arguments,
        UWorld* World,
        const FString& MapId,
        const FString& SnapshotId,
        TArray<TSharedPtr<FJsonValue>>& OutRecords,
        bool& OutScanTruncated,
        FUnrealMCPError& OutError);
};
