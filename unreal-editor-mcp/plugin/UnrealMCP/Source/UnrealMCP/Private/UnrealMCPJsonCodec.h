#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class FJsonObject;
class FJsonValue;

namespace UnrealMCP::JsonCodec
{
bool DecodeRecord(const TSharedPtr<FJsonObject>& Input, TSharedPtr<FUnrealMCPRecord>& OutRecord, FUnrealMCPError& OutError);
bool DecodeValue(const TSharedPtr<FJsonValue>& Input, TSharedPtr<FUnrealMCPValue>& OutValue, FUnrealMCPError& OutError);
TSharedPtr<FJsonObject> EncodeRecord(const TSharedPtr<FUnrealMCPRecord>& Input);
TSharedPtr<FJsonValue> EncodeValue(const TSharedPtr<FUnrealMCPValue>& Input);
bool Serialize(const TSharedPtr<FUnrealMCPRecord>& Input, FString& OutText);
bool SerializeValue(const TSharedPtr<FUnrealMCPValue>& Input, FString& OutText);
}

