#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "UnrealMCPProtocol.h"

class FUnrealMCPExtensionRegistry;

namespace UnrealMCP::BlueprintInspectionPrivate
{
bool BuildDiscovery(
    const FUnrealMCPRecord& Arguments,
    const FUnrealMCPExtensionRegistry* ExtensionRegistry,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
    FString& OutSnapshot,
    bool& OutScanTruncated,
    FUnrealMCPError& OutError);

bool BuildInspection(
    const FUnrealMCPRecord& Arguments,
    const FUnrealMCPExtensionRegistry* ExtensionRegistry,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
    FString& OutSnapshot,
    FString& OutBlueprintFamily,
    TSharedPtr<FUnrealMCPRecord>& OutFamilyCapabilities,
    bool& OutScanTruncated,
    FUnrealMCPError& OutError);
}
