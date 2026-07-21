#pragma once

#include "CoreMinimal.h"

namespace UnrealMCP
{
inline constexpr TCHAR Version[] = TEXT("0.9.0");
inline constexpr uint32 DefaultPort = 15485;
inline constexpr int32 MaxRequestBytes = 64 * 1024;
inline constexpr int32 MaxResponseBytes = 256 * 1024;
inline constexpr int32 MaxQueuedRequests = 8;
inline constexpr int32 MaxJsonDepth = 16;
inline constexpr int32 MaxStringLength = 4096;
inline constexpr double CommandDeadlineSeconds = 5.0;
inline constexpr double HeartbeatIntervalSeconds = 2.0;
inline constexpr int32 DefaultInspectPageSize = 25;
inline constexpr int32 MaxInspectPageSize = 100;
inline constexpr int32 MaxDiscoveryScan = 2048;
inline constexpr int32 MaxInspectRecords = 4096;
inline constexpr int32 MaxRetainedCursors = 32;
inline constexpr int32 MaxComponentDefaults = 16;
inline constexpr double CursorLifetimeSeconds = 30.0;
inline constexpr int32 MaxCompilerDiagnostics = 64;
inline constexpr int32 MaxDiagnosticChars = 512;
inline constexpr int32 MaxRetainedOperations = 128;
inline constexpr double OperationLifetimeSeconds = 15.0 * 60.0;
inline constexpr int32 MaxPropertyNames = 32;
inline constexpr int32 MaxVariableReferences = 64;
inline constexpr int32 DefaultActionResults = 20;
inline constexpr int32 MaxActionResults = 50;
inline constexpr int32 MaxActionScan = 20000;
inline constexpr int32 MaxRetainedActions = 256;
inline constexpr int32 MaxRetainedCatalogs = 32;
inline constexpr double ActionLifetimeSeconds = 60.0;
inline constexpr double ActionScanSeconds = 1.0;
inline constexpr int32 MaxConcurrentCatalogs = 1;
}
