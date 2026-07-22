#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

namespace UnrealMCP::BlueprintInspectionPrivate
{
bool BuildDiscovery(
    const FJsonObject& Arguments,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    FString& OutSnapshot,
    bool& OutScanTruncated,
    FUnrealMCPError& OutError);

bool BuildInspection(
    const FJsonObject& Arguments,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    FString& OutSnapshot,
    FString& OutBlueprintFamily,
    TSharedPtr<FJsonObject>& OutFamilyCapabilities,
    bool& OutScanTruncated,
    FUnrealMCPError& OutError);
}
