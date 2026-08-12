#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "EdGraph/EdGraphPin.h"
#include "UnrealMCPProtocol.h"

namespace UnrealMCP::K2TypeCodec
{
TSharedRef<FUnrealMCPRecord> EncodeType(const FEdGraphPinType& Type);

bool DecodeType(
    const TSharedPtr<FUnrealMCPRecord>& Value,
    FEdGraphPinType& OutType,
    FUnrealMCPError& OutError);

TSharedRef<FUnrealMCPRecord> EncodeDefault(
    const FEdGraphPinType& Type,
    const FString& DefaultText);

bool DecodeDefault(
    const FEdGraphPinType& Type,
    const TSharedPtr<FUnrealMCPRecord>& Value,
    FString& OutDefaultText,
    FUnrealMCPError& OutError);
}
