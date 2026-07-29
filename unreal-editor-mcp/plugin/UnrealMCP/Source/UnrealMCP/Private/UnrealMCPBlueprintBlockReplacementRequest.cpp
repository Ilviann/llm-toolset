#include "UnrealMCPBlueprintBlockReplacementRequest.h"

#include "UnrealMCPBlueprintMutationCommon.h"
#include "UnrealMCPVersion.h"

namespace UnrealMCP::BlueprintBlockReplacement
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

bool IsNodeKey(const FString& Value)
{
    if (Value.IsEmpty() || Value.Len() > 64 || !FChar::IsAlpha(Value[0])) return false;
    for (TCHAR Character : Value)
        if (!FChar::IsAlnum(Character) && Character != TEXT('_')) return false;
    return true;
}

bool IsEndpointKey(const FString& Value)
{
    return Value == TEXT("$entry") || Value == TEXT("$result") || IsNodeKey(Value);
}

bool ReadPosition(const TSharedPtr<FJsonObject>& Object, FPosition& Out, FUnrealMCPError& OutError)
{
    double X = 0.0;
    double Y = 0.0;
    if (!Object.IsValid() || !HasOnlyFields(*Object, {TEXT("x"), TEXT("y")})
        || !Object->TryGetNumberField(TEXT("x"), X) || !Object->TryGetNumberField(TEXT("y"), Y)
        || !FMath::IsFinite(X) || !FMath::IsFinite(Y)
        || !FMath::IsNearlyEqual(X, FMath::RoundToDouble(X))
        || !FMath::IsNearlyEqual(Y, FMath::RoundToDouble(Y))
        || FMath::Abs(X) > UnrealMCP::MaxGraphCoordinate
        || FMath::Abs(Y) > UnrealMCP::MaxGraphCoordinate)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Every replacement position must contain bounded integer x and y")};
        return false;
    }
    Out.X = static_cast<int32>(X);
    Out.Y = static_cast<int32>(Y);
    return true;
}

bool ReadEndpoint(const TSharedPtr<FJsonObject>& Object, FEndpoint& Out, FUnrealMCPError& OutError)
{
    if (!Object.IsValid() || !HasOnlyFields(*Object, {TEXT("node_key"), TEXT("pin_name")})
        || !Object->TryGetStringField(TEXT("node_key"), Out.NodeKey) || !IsEndpointKey(Out.NodeKey)
        || !Object->TryGetStringField(TEXT("pin_name"), Out.PinName)
        || Out.PinName.IsEmpty() || Out.PinName.Len() > 128)
    {
        OutError = {TEXT("invalid_pin"), TEXT("Replacement endpoints require one exact node key and bounded pin name")};
        return false;
    }
    return true;
}

bool ReadGuidArray(
    const FJsonObject& Arguments,
    const TCHAR* Name,
    int32 Maximum,
    TArray<FString>& Out,
    FUnrealMCPError& OutError)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Arguments.TryGetArrayField(Name, Values) || Values == nullptr || Values->Num() > Maximum)
    {
        OutError = {TEXT("graph_limit_exceeded"), FString::Printf(TEXT("%s exceeds its replacement boundary limit"), Name)};
        return false;
    }
    TSet<FString> Unique;
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        FString Id;
        if (!Value.IsValid() || !Value->TryGetString(Id) || !IsGuidString(Id, 32) || Unique.Contains(Id))
        {
            OutError = {TEXT("invalid_argument"), FString::Printf(TEXT("%s must contain unique stable identities"), Name)};
            return false;
        }
        Unique.Add(Id);
        Out.Add(Id);
    }
    Out.Sort();
    return true;
}
}

bool Decode(const TSharedPtr<FJsonObject>& Arguments, FRequest& Out, FUnrealMCPError& OutError)
{
    if (!Arguments.IsValid() || !HasOnlyFields(*Arguments, {
        TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("function_id"),
        TEXT("expected_function_fingerprint"), TEXT("entry_node_id"), TEXT("result_node_id"),
        TEXT("owned_node_ids"), TEXT("local_variable_ids"), TEXT("entry_position"), TEXT("result_position"),
        TEXT("nodes"), TEXT("pin_defaults"), TEXT("connections")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Function replacement arguments have an invalid exact shape")};
        return false;
    }
    FString OperationId;
    FString RawAssetPath;
    const TSharedPtr<FJsonObject>* EntryPosition = nullptr;
    const TSharedPtr<FJsonObject>* ResultPosition = nullptr;
    if (!Arguments->TryGetStringField(TEXT("operation_id"), OperationId) || !IsGuidString(OperationId, 32)
        || !Arguments->TryGetStringField(TEXT("asset_path"), RawAssetPath)
        || !NormalizeAssetPath(RawAssetPath, Out.AssetPath, Out.PackageName)
        || !Arguments->TryGetStringField(TEXT("expected_snapshot"), Out.ExpectedSnapshot)
        || !IsGuidString(Out.ExpectedSnapshot, 40)
        || !Arguments->TryGetStringField(TEXT("function_id"), Out.FunctionId) || !IsGuidString(Out.FunctionId, 32)
        || !Arguments->TryGetStringField(TEXT("expected_function_fingerprint"), Out.ExpectedFunctionFingerprint)
        || !IsGuidString(Out.ExpectedFunctionFingerprint, 40)
        || !Arguments->TryGetStringField(TEXT("entry_node_id"), Out.EntryNodeId) || !IsGuidString(Out.EntryNodeId, 32)
        || !Arguments->TryGetStringField(TEXT("result_node_id"), Out.ResultNodeId) || !IsGuidString(Out.ResultNodeId, 32)
        || !Arguments->TryGetObjectField(TEXT("entry_position"), EntryPosition) || EntryPosition == nullptr
        || !Arguments->TryGetObjectField(TEXT("result_position"), ResultPosition) || ResultPosition == nullptr
        || !ReadPosition(*EntryPosition, Out.EntryPosition, OutError)
        || !ReadPosition(*ResultPosition, Out.ResultPosition, OutError)
        || !ReadGuidArray(*Arguments, TEXT("owned_node_ids"), UnrealMCP::MaxFunctionReplacementOwnedNodes,
            Out.OwnedNodeIds, OutError)
        || !ReadGuidArray(*Arguments, TEXT("local_variable_ids"), UnrealMCP::MaxFunctionReplacementLocals,
            Out.LocalVariableIds, OutError))
    {
        if (OutError.Code.IsEmpty())
            OutError = {TEXT("invalid_argument"), TEXT("Function replacement identities or preconditions are invalid")};
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
    if (!Arguments->TryGetArrayField(TEXT("nodes"), Nodes) || Nodes == nullptr
        || Nodes->Num() > UnrealMCP::MaxFunctionReplacementNodes)
    {
        OutError = {TEXT("graph_limit_exceeded"), TEXT("The replacement node plan exceeds its published limit")};
        return false;
    }
    TSet<FString> NodeKeys;
    for (const TSharedPtr<FJsonValue>& Value : *Nodes)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        const TSharedPtr<FJsonObject>* Position = nullptr;
        FNodePlan Plan;
        if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr
            || !HasOnlyFields(**Object, {TEXT("key"), TEXT("action_id"), TEXT("position")})
            || !(*Object)->TryGetStringField(TEXT("key"), Plan.Key) || !IsNodeKey(Plan.Key)
            || NodeKeys.Contains(Plan.Key)
            || !(*Object)->TryGetStringField(TEXT("action_id"), Plan.ActionId) || !IsGuidString(Plan.ActionId, 32)
            || !(*Object)->TryGetObjectField(TEXT("position"), Position) || Position == nullptr
            || !ReadPosition(*Position, Plan.Position, OutError))
        {
            if (OutError.Code.IsEmpty())
                OutError = {TEXT("invalid_action"), TEXT("Each replacement node requires a unique key, action, and position")};
            return false;
        }
        NodeKeys.Add(Plan.Key);
        Out.Nodes.Add(MoveTemp(Plan));
    }

    const TArray<TSharedPtr<FJsonValue>>* Defaults = nullptr;
    if (!Arguments->TryGetArrayField(TEXT("pin_defaults"), Defaults) || Defaults == nullptr
        || Defaults->Num() > UnrealMCP::MaxFunctionReplacementDefaults)
    {
        OutError = {TEXT("graph_limit_exceeded"), TEXT("The replacement default plan exceeds its published limit")};
        return false;
    }
    TSet<FString> DefaultEndpoints;
    for (const TSharedPtr<FJsonValue>& Value : *Defaults)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        const TSharedPtr<FJsonObject>* Endpoint = nullptr;
        const TSharedPtr<FJsonObject>* Default = nullptr;
        FDefaultPlan Plan;
        if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr
            || !HasOnlyFields(**Object, {TEXT("endpoint"), TEXT("value")})
            || !(*Object)->TryGetObjectField(TEXT("endpoint"), Endpoint) || Endpoint == nullptr
            || !ReadEndpoint(*Endpoint, Plan.Endpoint, OutError)
            || !(*Object)->TryGetObjectField(TEXT("value"), Default) || Default == nullptr || !Default->IsValid())
            return false;
        const FString Identity = Plan.Endpoint.NodeKey + TEXT("|") + Plan.Endpoint.PinName;
        if (DefaultEndpoints.Contains(Identity))
        {
            OutError = {TEXT("invalid_pin"), TEXT("A replacement pin default endpoint may appear only once")};
            return false;
        }
        DefaultEndpoints.Add(Identity);
        Plan.Value = *Default;
        Out.Defaults.Add(MoveTemp(Plan));
    }

    const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
    if (!Arguments->TryGetArrayField(TEXT("connections"), Connections) || Connections == nullptr
        || Connections->Num() > UnrealMCP::MaxFunctionReplacementConnections)
    {
        OutError = {TEXT("graph_limit_exceeded"), TEXT("The replacement connection plan exceeds its published limit")};
        return false;
    }
    TSet<FString> ConnectionEndpoints;
    for (const TSharedPtr<FJsonValue>& Value : *Connections)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        const TSharedPtr<FJsonObject>* From = nullptr;
        const TSharedPtr<FJsonObject>* To = nullptr;
        FConnectionPlan Plan;
        if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr
            || !(*Object)->TryGetObjectField(TEXT("from"), From) || From == nullptr
            || !(*Object)->TryGetObjectField(TEXT("to"), To) || To == nullptr
            || !ReadEndpoint(*From, Plan.From, OutError) || !ReadEndpoint(*To, Plan.To, OutError))
            return false;
        if ((*Object)->HasField(TEXT("automatic_conversion")))
        {
            const TSharedPtr<FJsonObject>* Position = nullptr;
            if (!HasOnlyFields(**Object, {TEXT("from"), TEXT("to"), TEXT("automatic_conversion"), TEXT("conversion_position")})
                || !(*Object)->TryGetBoolField(TEXT("automatic_conversion"), Plan.bAutomaticConversion)
                || !Plan.bAutomaticConversion
                || !(*Object)->TryGetObjectField(TEXT("conversion_position"), Position) || Position == nullptr
                || !ReadPosition(*Position, Plan.ConversionPosition, OutError))
                return false;
        }
        else if (!HasOnlyFields(**Object, {TEXT("from"), TEXT("to")}))
        {
            OutError = {TEXT("invalid_argument"), TEXT("A direct replacement connection has an invalid shape")};
            return false;
        }
        const FString Identity = Plan.From.NodeKey + TEXT("|") + Plan.From.PinName + TEXT(">")
            + Plan.To.NodeKey + TEXT("|") + Plan.To.PinName;
        if (ConnectionEndpoints.Contains(Identity))
        {
            OutError = {TEXT("invalid_connection"), TEXT("A replacement connection may appear only once")};
            return false;
        }
        ConnectionEndpoints.Add(Identity);
        Out.Connections.Add(MoveTemp(Plan));
    }

    auto KeyExists = [&NodeKeys](const FString& Key)
    {
        return Key == TEXT("$entry") || Key == TEXT("$result") || NodeKeys.Contains(Key);
    };
    for (const FDefaultPlan& Plan : Out.Defaults)
        if (!KeyExists(Plan.Endpoint.NodeKey))
        {
            OutError = {TEXT("invalid_node"), TEXT("A pin default references an unknown replacement node key")};
            return false;
        }
    for (const FConnectionPlan& Plan : Out.Connections)
        if (!KeyExists(Plan.From.NodeKey) || !KeyExists(Plan.To.NodeKey))
        {
            OutError = {TEXT("invalid_node"), TEXT("A connection references an unknown replacement node key")};
            return false;
        }
    return true;
}
}
