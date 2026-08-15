#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPAssetFamilyRegistry.h"

namespace UnrealMCP::CommonUIWidgetTreeInspection
{
inline constexpr TCHAR Section[] = TEXT("commonui_widgets");

bool Inspect(
    const FUnrealMCPAssetFamilyInspectionContext& Context,
    FUnrealMCPAssetFamilyDocumentBuilder& Document,
    FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
    FUnrealMCPError& OutError);

FString BuildFingerprint(UObject* Asset);

#if WITH_DEV_AUTOMATION_TESTS
bool ValidateFrozenAllowlist(int32& OutFamilyCount, FUnrealMCPError& OutError);
#endif
}
