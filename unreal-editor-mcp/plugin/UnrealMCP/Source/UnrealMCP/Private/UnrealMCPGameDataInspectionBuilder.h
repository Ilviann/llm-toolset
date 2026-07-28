#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

namespace UnrealMCP::GameDataInspectionBuilder
{
bool GatherDependencies(const FString& PackageName, TArray<FString>& OutDependencies, bool& bOutTruncated);

bool Build(
    const FJsonObject& Arguments,
    FString& OutTarget,
    FString& OutObjectPath,
    FString& OutPackage,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    TArray<TSharedPtr<FJsonValue>>& OutSchema,
    FString& OutSnapshot,
    TSharedPtr<FJsonObject>& OutMetadata,
    FUnrealMCPError& OutError);

TSharedRef<FJsonObject> BuildEditResult(
    const FString& Target,
    const FString& ObjectPath,
    const FString& Snapshot);
}
