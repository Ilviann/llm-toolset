#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class FUnrealMCPRecord;
class UObject;
class UStruct;
struct FUnrealMCPError;

struct FUnrealMCPStructuredDataSource
{
    const UStruct* Type = nullptr;
    const void* Data = nullptr;
    UObject* Owner = nullptr;
    bool bRequireAuthoredProperty = true;
};

namespace UnrealMCP::StructuredDataInspection
{
UNREALMCPBLUEPRINT_API FString EncodeSelectorSegment(const FString& Input);

UNREALMCPBLUEPRINT_API bool BuildPropertyPage(
    const FUnrealMCPStructuredDataSource& Source,
    const FString& SelectorPrefix,
    int32 PageIndex,
    int32 PageSize,
    const FString& SnapshotId,
    TSharedPtr<FUnrealMCPRecord>& OutProperties,
    FUnrealMCPError& OutError);

UNREALMCPBLUEPRINT_API bool BuildSelectedPropertyPage(
    const FUnrealMCPStructuredDataSource& Source,
    const FString& SelectorPrefix,
    const TArray<FString>& PropertyNames,
    int32 PageIndex,
    int32 PageSize,
    const FString& SnapshotId,
    TSharedPtr<FUnrealMCPRecord>& OutProperties,
    FUnrealMCPError& OutError);

UNREALMCPBLUEPRINT_API bool BuildFieldValues(
    const FUnrealMCPStructuredDataSource& Source,
    const FString& SelectorPrefix,
    TSharedPtr<FUnrealMCPRecord>& OutValues,
    FUnrealMCPError& OutError);

UNREALMCPBLUEPRINT_API bool InspectField(
    const FUnrealMCPStructuredDataSource& Source,
    const FString& SelectorPrefix,
    const TArray<FString>& FieldSegments,
    const FString& CanonicalSelector,
    int32 PageIndex,
    int32 PageSize,
    bool bHasPaging,
    const FString& SnapshotId,
    TSharedPtr<FUnrealMCPRecord>& OutInspection,
    FUnrealMCPError& OutError);

UNREALMCPBLUEPRINT_API FString BuildSnapshot(
    const FUnrealMCPStructuredDataSource& Source,
    const FString& Identity);

UNREALMCPBLUEPRINT_API FString BuildSelectedSnapshot(
    const FUnrealMCPStructuredDataSource& Source,
    const FString& Identity,
    const TArray<FString>& PropertyNames);
}
