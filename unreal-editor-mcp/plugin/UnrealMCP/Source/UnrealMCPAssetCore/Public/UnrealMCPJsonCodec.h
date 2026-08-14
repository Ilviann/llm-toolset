#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class FJsonObject;
class FJsonValue;

namespace UnrealMCP::JsonCodec
{
UNREALMCPASSETCORE_API bool DecodeRecord(const TSharedPtr<FJsonObject>& Input, TSharedPtr<FUnrealMCPRecord>& OutRecord, FUnrealMCPError& OutError);
UNREALMCPASSETCORE_API bool DecodeValue(const TSharedPtr<FJsonValue>& Input, TSharedPtr<FUnrealMCPValue>& OutValue, FUnrealMCPError& OutError);
UNREALMCPASSETCORE_API TSharedPtr<FJsonObject> EncodeRecord(const TSharedPtr<FUnrealMCPRecord>& Input);
UNREALMCPASSETCORE_API TSharedPtr<FJsonValue> EncodeValue(const TSharedPtr<FUnrealMCPValue>& Input);
UNREALMCPASSETCORE_API bool Serialize(const TSharedPtr<FUnrealMCPRecord>& Input, FString& OutText);
UNREALMCPASSETCORE_API bool SerializeValue(const TSharedPtr<FUnrealMCPValue>& Input, FString& OutText);
}
