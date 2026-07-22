#pragma once

#include "UnrealMCPBlueprintActionCatalog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintActionFilter.h"
#include "BlueprintEventNodeSpawner.h"
#include "BlueprintFieldNodeSpawner.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintVariableNodeSpawner.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "K2Node_CallFunction.h"
#include "K2Node_BitmaskLiteral.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_CommutativeAssociativeBinaryOperator.h"
#include "K2Node_DoOnceMultiInput.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_ForEachElementInEnum.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MultiGate.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_Self.h"
#include "K2Node_Switch.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPVersion.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

namespace UnrealMCP::BlueprintActionCatalogPrivate
{
static bool HasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed)
{
    TSet<FString> Names;
    for (const TCHAR* Name : Allowed) Names.Add(Name);
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
        if (!Names.Contains(Pair.Key)) return false;
    return true;
}

static FString GuidString(const FGuid& Guid)
{
    return Guid.IsValid() ? Guid.ToString(EGuidFormats::Digits).ToLower() : FString();
}

static bool IsGuidString(const FString& Value, int32 Digits)
{
    if (Value.Len() != Digits) return false;
    for (TCHAR Character : Value)
        if (!FChar::IsHexDigit(Character) || FChar::IsUpper(Character)) return false;
    return true;
}

static bool NormalizeAssetPath(const FString& Input, FString& OutObjectPath)
{
    if (!Input.StartsWith(TEXT("/")) || Input.StartsWith(TEXT("//")) || Input.Contains(TEXT("..")) || Input.Contains(TEXT("\\")))
        return false;
    const FString PackageName = FPackageName::ObjectPathToPackageName(Input);
    if (!FPackageName::IsValidLongPackageName(PackageName, true)) return false;
    if (Input.Contains(TEXT(".")))
    {
        if (!FPackageName::IsValidObjectPath(Input)) return false;
        OutObjectPath = Input;
        return true;
    }
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    OutObjectPath = PackageName + TEXT(".") + AssetName;
    return FPackageName::IsValidObjectPath(OutObjectPath);
}

static bool ReadOptionalString(
    const FJsonObject& Object,
    const TCHAR* Name,
    int32 MaxLength,
    FString& OutValue,
    FUnrealMCPError& OutError)
{
    OutValue.Reset();
    if (!Object.HasField(Name)) return true;
    if (!Object.TryGetStringField(Name, OutValue) || OutValue.IsEmpty() || OutValue.Len() > MaxLength)
    {
        OutError = {TEXT("invalid_argument"), FString::Printf(TEXT("%s must be a non-empty bounded string"), Name)};
        return false;
    }
    return true;
}

static FString CanonicalOwnerPath(const UClass* OwnerClass, const UBlueprint* Blueprint)
{
    if (OwnerClass == nullptr) return FString();
    if (Blueprint != nullptr && OwnerClass == Blueprint->SkeletonGeneratedClass && Blueprint->GeneratedClass != nullptr)
        return Blueprint->GeneratedClass->GetPathName();
    return OwnerClass->GetPathName();
}

static FString CanonicalActionOwnerPath(FBlueprintActionInfo& ActionInfo, const UBlueprint* Blueprint)
{
    if (const UClass* OwnerClass = ActionInfo.GetOwnerClass())
    {
        return CanonicalOwnerPath(OwnerClass, Blueprint);
    }
    if (const UBlueprint* OwnerBlueprint = Cast<UBlueprint>(ActionInfo.GetActionOwner()))
    {
        return OwnerBlueprint->GeneratedClass != nullptr
            ? OwnerBlueprint->GeneratedClass->GetPathName()
            : OwnerBlueprint->GetPathName();
    }
    return ActionInfo.GetActionOwner() != nullptr ? ActionInfo.GetActionOwner()->GetPathName() : FString();
}

static FString QueryDigest(const FString& Material)
{
    FTCHARToUTF8 Encoded(*Material);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

static bool IsFunctionLiteral(const UFunction* Function)
{
    return Function != nullptr && Function->GetOwnerClass() == UKismetSystemLibrary::StaticClass()
        && Function->GetName().StartsWith(TEXT("MakeLiteral"), ESearchCase::CaseSensitive);
}

static bool IsFlowControlNode(const UClass* NodeClass, FBlueprintActionInfo& ActionInfo)
{
    if (NodeClass == nullptr) return false;
    if (NodeClass->IsChildOf(UK2Node_MacroInstance::StaticClass()))
    {
        const UBlueprint* MacroLibrary = Cast<UBlueprint>(ActionInfo.GetActionOwner());
        return MacroLibrary != nullptr && MacroLibrary->GetName() == TEXT("StandardMacros");
    }
    return NodeClass->IsChildOf(UK2Node_IfThenElse::StaticClass())
            || NodeClass->IsChildOf(UK2Node_ExecutionSequence::StaticClass())
            || NodeClass->IsChildOf(UK2Node_MultiGate::StaticClass())
            || NodeClass->IsChildOf(UK2Node_DoOnceMultiInput::StaticClass())
            || NodeClass->IsChildOf(UK2Node_Switch::StaticClass())
            || NodeClass->IsChildOf(UK2Node_ForEachElementInEnum::StaticClass());
}

static bool ClassifyAction(
    const UBlueprintNodeSpawner* Spawner,
    FBlueprintActionInfo& ActionInfo,
    FString& OutFamily,
    bool& bOutWildcard)
{
    bOutWildcard = false;
    if (const UBlueprintEventNodeSpawner* EventSpawner = Cast<UBlueprintEventNodeSpawner>(Spawner))
    {
        if (EventSpawner->IsForCustomEvent() || EventSpawner->GetEventFunction() == nullptr) return false;
        OutFamily = TEXT("event");
        return true;
    }
    const UClass* NodeClass = Spawner != nullptr ? Spawner->NodeClass.Get() : nullptr;
    if (NodeClass == nullptr) return false;
    if (NodeClass->IsChildOf(UK2Node_DynamicCast::StaticClass())
        || NodeClass->IsChildOf(UK2Node_ClassDynamicCast::StaticClass()))
    {
        OutFamily = TEXT("cast");
        return true;
    }
    if (IsFlowControlNode(NodeClass, ActionInfo))
    {
        OutFamily = TEXT("flow_control");
        return true;
    }
    const UFunction* Function = ActionInfo.GetAssociatedFunction();
    if (NodeClass->IsChildOf(UK2Node_PromotableOperator::StaticClass())
        || NodeClass->IsChildOf(UK2Node_CommutativeAssociativeBinaryOperator::StaticClass()))
    {
        OutFamily = TEXT("operator");
        bOutWildcard = NodeClass->IsChildOf(UK2Node_PromotableOperator::StaticClass());
        return Function != nullptr;
    }
    if (IsFunctionLiteral(Function)
        || NodeClass->IsChildOf(UK2Node_EnumLiteral::StaticClass())
        || NodeClass->IsChildOf(UK2Node_BitmaskLiteral::StaticClass())
        || NodeClass->IsChildOf(UK2Node_Self::StaticClass()))
    {
        OutFamily = TEXT("literal");
        return true;
    }
    if (Cast<UBlueprintFunctionNodeSpawner>(Spawner) != nullptr)
    {
        OutFamily = TEXT("function_call");
        return true;
    }
    if (const UBlueprintVariableNodeSpawner* Variable = Cast<UBlueprintVariableNodeSpawner>(Spawner))
    {
        const UClass* VariableNodeClass = Variable->NodeClass.Get();
        if (VariableNodeClass != nullptr && VariableNodeClass->IsChildOf(UK2Node_VariableGet::StaticClass()))
        {
            OutFamily = TEXT("variable_get");
            return true;
        }
        if (VariableNodeClass != nullptr && VariableNodeClass->IsChildOf(UK2Node_VariableSet::StaticClass()))
        {
            OutFamily = TEXT("variable_set");
            return true;
        }
    }
    return false;
}

static FString ActionSignature(
    const FString& Family,
    const FString& OwnerClass,
    const FString& MemberName,
    const UObject* ActionOwner,
    const UBlueprintNodeSpawner* Spawner)
{
    const FString Material = Family + TEXT("|") + OwnerClass + TEXT("|") + MemberName + TEXT("|")
        + (ActionOwner != nullptr ? ActionOwner->GetPathName() : FString()) + TEXT("|")
        + (Spawner != nullptr ? Spawner->GetSpawnerSignature().ToString() : FString());
    return QueryDigest(Material);
}

static TSharedRef<FJsonObject> MakeResult(
    const FString& BridgeInstanceId,
    const FString& AssetPath,
    const FString& BlueprintFamily,
    const FString& GraphId,
    const FString& SnapshotId,
    const TArray<TSharedPtr<FJsonValue>>& Actions,
    int32 ScannedCount,
    bool bTruncated,
    bool bTimedOut,
    int32 ExpiresInMs)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("bridge_instance_id"), BridgeInstanceId);
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("blueprint_family"), BlueprintFamily);
    Result->SetStringField(TEXT("graph_id"), GraphId);
    Result->SetStringField(TEXT("snapshot_id"), SnapshotId);
    Result->SetArrayField(TEXT("actions"), Actions);
    Result->SetNumberField(TEXT("returned_count"), Actions.Num());
    Result->SetNumberField(TEXT("scanned_count"), ScannedCount);
    Result->SetBoolField(TEXT("truncated"), bTruncated);
    Result->SetBoolField(TEXT("timed_out"), bTimedOut);
    Result->SetNumberField(TEXT("action_expires_in_ms"), ExpiresInMs);
    return Result;
}
}
