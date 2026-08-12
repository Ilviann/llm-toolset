#pragma once

#include "CoreMinimal.h"

class FUnrealMCPValue;
class FProperty;
class UObject;
class FUnrealMCPRecord;
struct FUnrealMCPError;

namespace UnrealMCP::PropertyCodec
{
bool IsSupportedEditable(const FProperty* Property, FString& OutKind);
bool IsIdenticalToArchetype(const UObject* Object, const FProperty* Property);
bool ExportValueText(const UObject* Object, const FProperty* Property, FString& OutText);
TSharedRef<FUnrealMCPRecord> Encode(UObject* Object, FProperty* Property);
bool Set(UObject* Object, const FString& PropertyName, const TSharedPtr<FUnrealMCPValue>& Value,
    TSharedPtr<FUnrealMCPRecord>& OutChanged, FUnrealMCPError& OutError);
}
