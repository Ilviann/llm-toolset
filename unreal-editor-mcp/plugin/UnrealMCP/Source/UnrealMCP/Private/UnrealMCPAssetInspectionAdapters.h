#pragma once

#include "CoreMinimal.h"

class FUnrealMCPAssetFamilyDocumentBuilder;
class FUnrealMCPAssetFamilyRegistry;
class FUnrealMCPRecord;
class UObject;
struct FUnrealMCPError;

namespace UnrealMCP::AssetInspection
{
bool RegisterBuiltInAdapters(
    FUnrealMCPAssetFamilyRegistry& Registry,
    FUnrealMCPError& OutError);

FString BuildStableSnapshot(UObject* Asset);

bool EncodeDocument(
    const FUnrealMCPAssetFamilyDocumentBuilder& Document,
    const FString& ExpectedObjectPath,
    const FString& ExpectedSnapshot,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError);
}
