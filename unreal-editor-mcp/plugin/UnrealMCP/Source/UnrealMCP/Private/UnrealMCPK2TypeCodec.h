#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraphPin.h"
#include "UnrealMCPProtocol.h"

namespace UnrealMCP::K2TypeCodec
{
TSharedRef<FJsonObject> EncodeType(const FEdGraphPinType& Type);

bool DecodeType(
    const TSharedPtr<FJsonObject>& Value,
    FEdGraphPinType& OutType,
    FUnrealMCPError& OutError);

TSharedRef<FJsonObject> EncodeDefault(
    const FEdGraphPinType& Type,
    const FString& DefaultText);

bool DecodeDefault(
    const FEdGraphPinType& Type,
    const TSharedPtr<FJsonObject>& Value,
    FString& OutDefaultText,
    FUnrealMCPError& OutError);
}
