#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

namespace UnrealMCP::GameDataInspectionBuilder
{
bool GatherDependencies(const FString& PackageName, TArray<FString>& OutDependencies, bool& bOutTruncated);

bool Build(
    const FUnrealMCPRecord& Arguments,
    FString& OutTarget,
    FString& OutObjectPath,
    FString& OutPackage,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutSchema,
    FString& OutSnapshot,
    TSharedPtr<FUnrealMCPRecord>& OutMetadata,
    FUnrealMCPError& OutError);

TSharedRef<FUnrealMCPRecord> BuildEditResult(
    const FString& Target,
    const FString& ObjectPath,
    const FString& Snapshot);
}
