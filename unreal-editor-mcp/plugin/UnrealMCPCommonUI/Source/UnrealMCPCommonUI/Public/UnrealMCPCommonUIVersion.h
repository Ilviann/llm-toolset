#pragma once

#include "CoreMinimal.h"

namespace UnrealMCPCommonUI
{
inline constexpr TCHAR Version[] = TEXT("0.3.0");
inline constexpr int32 CompanionApiVersion = 2;
inline constexpr int32 ExtensionSchemaRevision = 2;
inline constexpr int32 MaxRootInspectionRecords = 3;
inline constexpr int32 MaxWidgetTreeWidgets = 128;
inline constexpr int32 MaxPropertiesPerWidget = 48;
inline constexpr int32 MaxInputActionReferences = 32;
inline constexpr int32 MaxInspectionRecords = MaxRootInspectionRecords + MaxWidgetTreeWidgets;
inline constexpr int32 MaxInspectedProperties = 17 + MaxWidgetTreeWidgets * MaxPropertiesPerWidget;
}
