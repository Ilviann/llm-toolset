#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

namespace UnrealMCP::BlueprintBlockReplacement
{
enum class ETargetKind : uint8
{
    Function,
    Macro,
    CustomEvent,
    Event,
};

enum class ELayoutPolicy : uint8
{
    Explicit,
    LayeredV1,
};

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
    TSharedPtr<FUnrealMCPRecord> Value;
};

struct FConnectionPlan
{
    FEndpoint From;
    FEndpoint To;
    bool bAutomaticConversion = false;
    FPosition ConversionPosition;
};

struct FExternalEndpoint
{
    FString NodeId;
    FString PinId;
};

struct FExternalConnectionPlan
{
    bool bExternalFrom = false;
    FExternalEndpoint External;
    FEndpoint Internal;
};

struct FRequest
{
    ETargetKind TargetKind = ETargetKind::Function;
    ELayoutPolicy LayoutPolicy = ELayoutPolicy::Explicit;
    bool bLegacyFunctionShape = false;
    FString AssetPath;
    FString PackageName;
    FString ExpectedSnapshot;
    FString LogicUnitId;
    FString GraphId;
    FString ExpectedLogicUnitFingerprint;
    FString EntryNodeId;
    FString ResultNodeId;
    TArray<FString> OwnedNodeIds;
    TArray<FString> LocalVariableIds;
    FPosition EntryPosition;
    FPosition ResultPosition;
    TArray<FNodePlan> Nodes;
    TArray<FDefaultPlan> Defaults;
    TArray<FConnectionPlan> Connections;
    TArray<FExternalConnectionPlan> ExternalConnections;
};

FString TargetKindString(ETargetKind Kind);
bool Decode(const TSharedPtr<FUnrealMCPRecord>& Arguments, FRequest& OutRequest, FUnrealMCPError& OutError);
}
