#pragma once

#include "CoreMinimal.h"

namespace UnrealMCPGAS
{
inline constexpr TCHAR Version[] = TEXT("0.2.0");
inline constexpr int32 CompanionApiVersion = 1;
inline constexpr int32 ExtensionSchemaRevision = 1;
inline constexpr int32 MaxTagsPerContainer = 256;
inline constexpr int32 MaxAbilityTriggers = 128;
inline constexpr int32 MaxTagScan = 2048;
inline constexpr int32 MaxTriggerScan = 1024;
inline constexpr int32 MaxInspectionRecords = 4;
inline constexpr int32 MaxGameplayEffectInspectionRecords = 11;
inline constexpr int32 MaxGameplayEffectModifiers = 128;
inline constexpr int32 MaxGameplayEffectExecutions = 64;
inline constexpr int32 MaxGameplayEffectCues = 128;
inline constexpr int32 MaxGameplayEffectComponents = 64;
inline constexpr int32 MaxGameplayEffectGrantedAbilities = 128;
inline constexpr int32 MaxGameplayEffectReferences = 256;
inline constexpr int32 MaxGameplayEffectRequirements = 128;
inline constexpr int32 MaxGameplayEffectRelationships = 64;
inline constexpr int32 MaxGameplayEffectCollectionScan = 2048;
inline constexpr int32 MaxGameplayEffectChainDepth = 8;
inline constexpr int32 MaxGameplayEffectChainAssets = 128;
}
