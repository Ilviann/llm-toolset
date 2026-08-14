#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class IUnrealMCPBlueprintExtensionProvider;

namespace UnrealMCP::BlueprintInspectionPrivate
{
bool BuildDiscovery(
    const FUnrealMCPRecord& Arguments,
    const IUnrealMCPBlueprintExtensionProvider* ExtensionRegistry,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
    FString& OutSnapshot,
    bool& OutScanTruncated,
    FUnrealMCPError& OutError);

bool BuildInspection(
    const FUnrealMCPRecord& Arguments,
    const IUnrealMCPBlueprintExtensionProvider* ExtensionRegistry,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
    FString& OutSnapshot,
    FString& OutBlueprintFamily,
    TSharedPtr<FUnrealMCPRecord>& OutFamilyCapabilities,
    bool& OutScanTruncated,
    FUnrealMCPError& OutError);
}
