#pragma once

#include "CoreMinimal.h"

class FJsonValue;
class FProperty;
class UObject;
class FJsonObject;
struct FUnrealMCPError;

namespace UnrealMCP::PropertyCodec
{
bool IsSupportedEditable(const FProperty* Property, FString& OutKind);
TSharedRef<FJsonObject> Encode(UObject* Object, FProperty* Property);
bool Set(UObject* Object, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value,
    TSharedPtr<FJsonObject>& OutChanged, FUnrealMCPError& OutError);
}
