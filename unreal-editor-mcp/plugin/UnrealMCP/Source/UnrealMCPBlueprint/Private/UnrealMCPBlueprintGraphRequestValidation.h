#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

namespace UnrealMCP::BlueprintGraphRequestValidation
{
struct FRequest
{
    FString Operation;
    FString AssetPath;
    FString PackageName;
    FString ExpectedSnapshot;
    FString GraphId;
    FString ActionId;
    FString NodeId;
    FString PinId;
    FString FromNodeId;
    FString FromPinId;
    FString ToNodeId;
    FString ToPinId;
    TSharedPtr<FUnrealMCPRecord> Default;
    bool bAutomaticConversion = false;
    int32 X = 0;
    int32 Y = 0;
};

bool Decode(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    FRequest& OutRequest,
    FUnrealMCPError& OutError);
}
