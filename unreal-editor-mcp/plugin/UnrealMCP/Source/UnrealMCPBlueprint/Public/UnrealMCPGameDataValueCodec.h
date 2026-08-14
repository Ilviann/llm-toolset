#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class FUnrealMCPRecord;
class FProperty;
class UScriptStruct;
struct FUnrealMCPError;

namespace UnrealMCP::GameDataValueCodec
{
UNREALMCPBLUEPRINT_API TSharedRef<FUnrealMCPRecord> EncodeType(const FProperty* Property);
UNREALMCPBLUEPRINT_API bool Encode(const FProperty* Property, const void* Value, int32 Depth,
    TSharedPtr<FUnrealMCPValue>& OutValue, FUnrealMCPError& OutError);
UNREALMCPBLUEPRINT_API bool Decode(const FProperty* Property, void* Value, const TSharedPtr<FUnrealMCPValue>& Input,
    int32 Depth, FUnrealMCPError& OutError);
UNREALMCPBLUEPRINT_API bool ApplyFields(const UScriptStruct* Struct, void* Data, const TSharedPtr<FUnrealMCPRecord>& Fields,
    FUnrealMCPError& OutError);
UNREALMCPBLUEPRINT_API TSharedRef<FUnrealMCPRecord> EncodeFields(const UScriptStruct* Struct, const void* Data,
    int32 Depth, FUnrealMCPError& OutError, bool& bSucceeded);
}
