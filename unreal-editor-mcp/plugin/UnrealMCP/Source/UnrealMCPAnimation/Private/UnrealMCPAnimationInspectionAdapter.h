#pragma once

class FUnrealMCPAssetFamilyRegistry;
struct FUnrealMCPError;

namespace UnrealMCP::AnimationInspection
{
bool RegisterAdapter(FUnrealMCPAssetFamilyRegistry& Registry, FUnrealMCPError& OutError);
}
