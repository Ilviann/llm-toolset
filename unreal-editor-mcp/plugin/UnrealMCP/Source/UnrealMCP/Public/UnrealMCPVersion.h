#pragma once

#include "CoreMinimal.h"

namespace UnrealMCP
{
inline constexpr TCHAR Version[] = TEXT("0.1.0");
inline constexpr uint32 DefaultPort = 15485;
inline constexpr int32 MaxRequestBytes = 64 * 1024;
inline constexpr int32 MaxResponseBytes = 256 * 1024;
inline constexpr int32 MaxQueuedRequests = 8;
inline constexpr int32 MaxJsonDepth = 16;
inline constexpr int32 MaxStringLength = 4096;
inline constexpr double CommandDeadlineSeconds = 5.0;
inline constexpr double HeartbeatIntervalSeconds = 2.0;
}
