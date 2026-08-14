#pragma once

class FUnrealMCPAssetFamilyRegistry;
struct FUnrealMCPError;

namespace UnrealMCP::AssetCore
{
UNREALMCPASSETCORE_API bool RegisterNeutralAssetAdapter(
    FUnrealMCPAssetFamilyRegistry& Registry,
    FUnrealMCPError& OutError);
}
