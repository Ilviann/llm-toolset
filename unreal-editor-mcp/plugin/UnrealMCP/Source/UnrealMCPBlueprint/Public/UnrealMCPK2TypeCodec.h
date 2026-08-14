#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"
#include "EdGraph/EdGraphPin.h"

namespace UnrealMCP::K2TypeCodec
{
UNREALMCPBLUEPRINT_API TSharedRef<FUnrealMCPRecord> EncodeType(const FEdGraphPinType& Type);

UNREALMCPBLUEPRINT_API bool DecodeType(
    const TSharedPtr<FUnrealMCPRecord>& Value,
    FEdGraphPinType& OutType,
    FUnrealMCPError& OutError);

UNREALMCPBLUEPRINT_API TSharedRef<FUnrealMCPRecord> EncodeDefault(
    const FEdGraphPinType& Type,
    const FString& DefaultText);

UNREALMCPBLUEPRINT_API bool DecodeDefault(
    const FEdGraphPinType& Type,
    const TSharedPtr<FUnrealMCPRecord>& Value,
    FString& OutDefaultText,
    FUnrealMCPError& OutError);
}
