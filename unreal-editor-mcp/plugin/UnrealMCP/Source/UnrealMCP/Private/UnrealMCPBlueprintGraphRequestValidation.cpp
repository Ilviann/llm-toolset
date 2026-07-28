#include "UnrealMCPBlueprintGraphRequestValidation.h"

#include "UnrealMCPBlueprintMutationCommon.h"
#include "UnrealMCPVersion.h"

namespace UnrealMCP::BlueprintGraphRequestValidation
{
using namespace UnrealMCP::BlueprintMutationPrivate;

namespace
{
bool IsGuidString(const FString& Value, int32 Digits)
{
    if (Value.Len() != Digits) return false;
    for (TCHAR Character : Value)
        if (!FChar::IsHexDigit(Character) || FChar::IsUpper(Character)) return false;
    return true;
}

bool ReadPosition(const FJsonObject& Arguments, int32& OutX, int32& OutY, FUnrealMCPError& OutError)
{
    const TSharedPtr<FJsonObject>* Position = nullptr;
    double X = 0.0;
    double Y = 0.0;
    if (!Arguments.TryGetObjectField(TEXT("position"), Position) || Position == nullptr || !Position->IsValid()
        || !HasOnlyFields(**Position, {TEXT("x"), TEXT("y")})
        || !(*Position)->TryGetNumberField(TEXT("x"), X) || !(*Position)->TryGetNumberField(TEXT("y"), Y)
        || !FMath::IsFinite(X) || !FMath::IsFinite(Y)
        || !FMath::IsNearlyEqual(X, FMath::RoundToDouble(X)) || !FMath::IsNearlyEqual(Y, FMath::RoundToDouble(Y))
        || FMath::Abs(X) > UnrealMCP::MaxGraphCoordinate || FMath::Abs(Y) > UnrealMCP::MaxGraphCoordinate)
    {
        OutError = {TEXT("invalid_argument"), TEXT("position must contain bounded integer x and y coordinates")};
        return false;
    }
    OutX = static_cast<int32>(X);
    OutY = static_cast<int32>(Y);
    return true;
}
}

bool Decode(const TSharedPtr<FJsonObject>& Arguments, FRequest& Out, FUnrealMCPError& OutError)
{
    if (!Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("operation"), Out.Operation)
        || (Out.Operation != TEXT("add_node") && Out.Operation != TEXT("move_node") && Out.Operation != TEXT("remove_node")
            && Out.Operation != TEXT("set_pin_default") && Out.Operation != TEXT("connect_pins")
            && Out.Operation != TEXT("disconnect_pins")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Graph edit operation is invalid")};
        return false;
    }
    const bool bAdd = Out.Operation == TEXT("add_node");
    const bool bMove = Out.Operation == TEXT("move_node");
    const bool bSetDefault = Out.Operation == TEXT("set_pin_default");
    const bool bConnect = Out.Operation == TEXT("connect_pins");
    const bool bConnection = bConnect || Out.Operation == TEXT("disconnect_pins");
    const bool bShapeValid = bAdd
        ? HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("graph_id"), TEXT("action_id"), TEXT("position")})
        : bMove
            ? HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("graph_id"), TEXT("node_id"), TEXT("position")})
            : bSetDefault
                ? HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("graph_id"), TEXT("node_id"), TEXT("pin_id"), TEXT("default")})
                : bConnect
                    ? HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("graph_id"),
                        TEXT("from_node_id"), TEXT("from_pin_id"), TEXT("to_node_id"), TEXT("to_pin_id"), TEXT("automatic_conversion")})
                    : bConnection
                        ? HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("graph_id"),
                            TEXT("from_node_id"), TEXT("from_pin_id"), TEXT("to_node_id"), TEXT("to_pin_id")})
                        : HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation"), TEXT("graph_id"), TEXT("node_id")});
    if (!bShapeValid)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Graph edit arguments have an invalid shape")};
        return false;
    }
    FString OperationId;
    FString RawAssetPath;
    if (!Arguments->TryGetStringField(TEXT("operation_id"), OperationId) || !IsGuidString(OperationId, 32)
        || !Arguments->TryGetStringField(TEXT("asset_path"), RawAssetPath)
        || !NormalizeAssetPath(RawAssetPath, Out.AssetPath, Out.PackageName)
        || !Arguments->TryGetStringField(TEXT("expected_snapshot"), Out.ExpectedSnapshot) || !IsGuidString(Out.ExpectedSnapshot, 40)
        || !Arguments->TryGetStringField(TEXT("graph_id"), Out.GraphId) || !IsGuidString(Out.GraphId, 32))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Graph edit identities or asset path are invalid")};
        return false;
    }
    if (bAdd)
    {
        if (!Arguments->TryGetStringField(TEXT("action_id"), Out.ActionId) || !IsGuidString(Out.ActionId, 32)
            || !ReadPosition(*Arguments, Out.X, Out.Y, OutError))
        {
            if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_action"), TEXT("action_id is invalid")};
            return false;
        }
    }
    else if (bMove || Out.Operation == TEXT("remove_node") || bSetDefault)
    {
        if (!Arguments->TryGetStringField(TEXT("node_id"), Out.NodeId) || !IsGuidString(Out.NodeId, 32))
        {
            OutError = {TEXT("invalid_node"), TEXT("node_id is invalid")};
            return false;
        }
        if (bMove && !ReadPosition(*Arguments, Out.X, Out.Y, OutError)) return false;
        if (bSetDefault)
        {
            const TSharedPtr<FJsonObject>* Default = nullptr;
            if (!Arguments->TryGetStringField(TEXT("pin_id"), Out.PinId) || !IsGuidString(Out.PinId, 32)
                || !Arguments->TryGetObjectField(TEXT("default"), Default) || Default == nullptr || !Default->IsValid())
            {
                OutError = {TEXT("invalid_pin"), TEXT("pin_id and default must identify one stable pin and typed value")};
                return false;
            }
            Out.Default = *Default;
        }
    }
    else
    {
        if (!Arguments->TryGetStringField(TEXT("from_node_id"), Out.FromNodeId) || !IsGuidString(Out.FromNodeId, 32)
            || !Arguments->TryGetStringField(TEXT("from_pin_id"), Out.FromPinId) || !IsGuidString(Out.FromPinId, 32)
            || !Arguments->TryGetStringField(TEXT("to_node_id"), Out.ToNodeId) || !IsGuidString(Out.ToNodeId, 32)
            || !Arguments->TryGetStringField(TEXT("to_pin_id"), Out.ToPinId) || !IsGuidString(Out.ToPinId, 32))
        {
            OutError = {TEXT("invalid_pin"), TEXT("Direct connections require stable from/to node and pin identities")};
            return false;
        }
        if (bConnect && Arguments->HasField(TEXT("automatic_conversion"))
            && !Arguments->TryGetBoolField(TEXT("automatic_conversion"), Out.bAutomaticConversion))
        {
            OutError = {TEXT("invalid_argument"), TEXT("automatic_conversion must be a Boolean when supplied")};
            return false;
        }
    }
    return true;
}
}
