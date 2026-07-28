#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

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
    TSharedPtr<FJsonObject> Default;
    bool bAutomaticConversion = false;
    int32 X = 0;
    int32 Y = 0;
};

bool Decode(
    const TSharedPtr<FJsonObject>& Arguments,
    FRequest& OutRequest,
    FUnrealMCPError& OutError);
}
