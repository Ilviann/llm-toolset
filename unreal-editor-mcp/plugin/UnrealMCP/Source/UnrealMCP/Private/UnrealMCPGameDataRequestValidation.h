#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

namespace UnrealMCP::GameDataRequestValidation
{
bool HasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed);
bool ValidateEditShape(
    const FJsonObject& Arguments,
    const FString& Target,
    const FString& Operation,
    FUnrealMCPError& OutError);
FString ObjectPathForPackage(const FString& PackageName);
bool NormalizePackagePath(const FString& Input, FString& OutPackage);
bool NormalizeAssetPath(const FString& Input, FString& OutObject, FString& OutPackage);
bool ReadPageSize(const FJsonObject& Arguments, int32& OutPageSize, FUnrealMCPError& OutError);
bool ParseGuidField(const FJsonObject& Arguments, const TCHAR* Name, FGuid& OutGuid, FUnrealMCPError& OutError);
bool ValidName(const FString& Value);
}
