#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

namespace UnrealMCP::GameDataRequestValidation
{
bool HasOnlyFields(const FUnrealMCPRecord& Object, std::initializer_list<const TCHAR*> Allowed);
bool ValidateEditShape(
    const FUnrealMCPRecord& Arguments,
    const FString& Target,
    const FString& Operation,
    FUnrealMCPError& OutError);
FString ObjectPathForPackage(const FString& PackageName);
bool NormalizePackagePath(const FString& Input, FString& OutPackage);
bool NormalizeAssetPath(const FString& Input, FString& OutObject, FString& OutPackage);
bool ReadPageSize(const FUnrealMCPRecord& Arguments, int32& OutPageSize, FUnrealMCPError& OutError);
bool ParseGuidField(const FUnrealMCPRecord& Arguments, const TCHAR* Name, FGuid& OutGuid, FUnrealMCPError& OutError);
bool ValidName(const FString& Value);
}
