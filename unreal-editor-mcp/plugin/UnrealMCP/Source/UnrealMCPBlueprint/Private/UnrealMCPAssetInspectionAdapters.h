#pragma once

#include "CoreMinimal.h"

class FUnrealMCPAssetFamilyDocumentBuilder;
class FUnrealMCPAssetFamilyRegistry;
class FUnrealMCPRecord;
class UObject;
struct FUnrealMCPError;

namespace UnrealMCP::AssetInspection
{
bool RegisterBlueprintAdapter(
    FUnrealMCPAssetFamilyRegistry& Registry,
    FUnrealMCPError& OutError);
bool RegisterBuiltInAdapters(
    FUnrealMCPAssetFamilyRegistry& Registry,
    FUnrealMCPError& OutError);
}
