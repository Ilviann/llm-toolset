#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

namespace UnrealMCP::BlueprintBlockReplacement
{
struct FPosition
{
    int32 X = 0;
    int32 Y = 0;
};

struct FEndpoint
{
    FString NodeKey;
    FString PinName;
};

struct FNodePlan
{
    FString Key;
    FString ActionId;
    FPosition Position;
};

struct FDefaultPlan
{
    FEndpoint Endpoint;
    TSharedPtr<FJsonObject> Value;
};

struct FConnectionPlan
{
    FEndpoint From;
    FEndpoint To;
    bool bAutomaticConversion = false;
    FPosition ConversionPosition;
};

struct FRequest
{
    FString AssetPath;
    FString PackageName;
    FString ExpectedSnapshot;
    FString FunctionId;
    FString ExpectedFunctionFingerprint;
    FString EntryNodeId;
    FString ResultNodeId;
    TArray<FString> OwnedNodeIds;
    TArray<FString> LocalVariableIds;
    FPosition EntryPosition;
    FPosition ResultPosition;
    TArray<FNodePlan> Nodes;
    TArray<FDefaultPlan> Defaults;
    TArray<FConnectionPlan> Connections;
};

bool Decode(const TSharedPtr<FJsonObject>& Arguments, FRequest& OutRequest, FUnrealMCPError& OutError);
}
