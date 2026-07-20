#pragma once

#include "CoreMinimal.h"

namespace UnrealMCP::Compatibility
{
bool SupportsCurrentEngine();
FString EngineApiLine();
bool SecureTokenFile(const FString& Path);
}
