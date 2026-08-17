#pragma once

#include "CoreMinimal.h"

namespace UnrealMCPAI
{
inline constexpr TCHAR Version[] = TEXT("0.1.0");
inline constexpr int32 CompanionApiVersion = 2;
inline constexpr int32 ExtensionSchemaRevision = 2;
inline constexpr int32 MaxTreeNodes = 512;
inline constexpr int32 MaxTreeDepth = 32;
inline constexpr int32 MaxBlackboardKeys = 256;
inline constexpr int32 MaxParentDepth = 8;
inline constexpr int32 MaxQueryOptions = 64;
inline constexpr int32 MaxQueryTests = 256;
inline constexpr int32 MaxSelectorsPerObject = 16;
inline constexpr int32 MaxContextsPerObject = 16;
inline constexpr int32 MaxDiagnostics = 64;
inline constexpr int32 MaxPersistedProperties = 64;
inline constexpr int32 MaxExportedPropertyBytes = 4096;
}
