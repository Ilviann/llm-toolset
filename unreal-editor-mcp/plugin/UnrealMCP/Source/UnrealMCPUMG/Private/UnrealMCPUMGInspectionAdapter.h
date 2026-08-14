#pragma once

class FUnrealMCPAssetFamilyRegistry;
struct FUnrealMCPError;

namespace UnrealMCP::UMGInspection
{
bool RegisterAdapter(
    FUnrealMCPAssetFamilyRegistry& Registry,
    FUnrealMCPError& OutError);
}
