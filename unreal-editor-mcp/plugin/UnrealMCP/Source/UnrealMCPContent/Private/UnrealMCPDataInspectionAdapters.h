#pragma once

#include "CoreMinimal.h"

class FUnrealMCPAssetFamilyRegistry;
struct FUnrealMCPError;

namespace UnrealMCP::DataInspection
{
bool RegisterAdapters(FUnrealMCPAssetFamilyRegistry& Registry, FUnrealMCPError& OutError);
}
