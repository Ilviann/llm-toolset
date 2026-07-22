#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class FJsonObject;
class FProperty;
class UScriptStruct;
struct FUnrealMCPError;

namespace UnrealMCP::GameDataValueCodec
{
TSharedRef<FJsonObject> EncodeType(const FProperty* Property);
bool Encode(const FProperty* Property, const void* Value, int32 Depth,
    TSharedPtr<FJsonValue>& OutValue, FUnrealMCPError& OutError);
bool Decode(const FProperty* Property, void* Value, const TSharedPtr<FJsonValue>& Input,
    int32 Depth, FUnrealMCPError& OutError);
bool ApplyFields(const UScriptStruct* Struct, void* Data, const TSharedPtr<FJsonObject>& Fields,
    FUnrealMCPError& OutError);
TSharedRef<FJsonObject> EncodeFields(const UScriptStruct* Struct, const void* Data,
    int32 Depth, FUnrealMCPError& OutError, bool& bSucceeded);
}
