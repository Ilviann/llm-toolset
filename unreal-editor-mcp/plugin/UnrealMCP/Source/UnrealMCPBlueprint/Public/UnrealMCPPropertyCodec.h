#pragma once

#include "CoreMinimal.h"

class FUnrealMCPValue;
class FProperty;
class UObject;
class FUnrealMCPRecord;
struct FUnrealMCPError;

namespace UnrealMCP::PropertyCodec
{
UNREALMCPBLUEPRINT_API bool IsSupportedEditable(const FProperty* Property, FString& OutKind);
UNREALMCPBLUEPRINT_API bool IsIdenticalToArchetype(const UObject* Object, const FProperty* Property);
UNREALMCPBLUEPRINT_API bool ExportValueText(const UObject* Object, const FProperty* Property, FString& OutText);
UNREALMCPBLUEPRINT_API TSharedRef<FUnrealMCPRecord> Encode(UObject* Object, FProperty* Property);
UNREALMCPBLUEPRINT_API bool Set(UObject* Object, const FString& PropertyName, const TSharedPtr<FUnrealMCPValue>& Value,
    TSharedPtr<FUnrealMCPRecord>& OutChanged, FUnrealMCPError& OutError);
}
