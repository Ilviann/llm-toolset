#pragma once

#include "UnrealMCPBlueprintActionCatalog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintActionFilter.h"
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
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "EdGraphSchema_K2.h"
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

static FString QueryDigest(const FString& Material)
{
    FTCHARToUTF8 Encoded(*Material);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

static bool IsCoreFamily(const UBlueprintNodeSpawner* Spawner, FString& OutFamily)
{
    if (Cast<UBlueprintFunctionNodeSpawner>(Spawner) != nullptr)
    {
        OutFamily = TEXT("function_call");
        return true;
    }
    if (const UBlueprintVariableNodeSpawner* Variable = Cast<UBlueprintVariableNodeSpawner>(Spawner))
    {
        const UClass* NodeClass = Variable->NodeClass.Get();
        if (NodeClass != nullptr && NodeClass->IsChildOf(UK2Node_VariableGet::StaticClass()))
        {
            OutFamily = TEXT("variable_get");
            return true;
        }
        if (NodeClass != nullptr && NodeClass->IsChildOf(UK2Node_VariableSet::StaticClass()))
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
    const UBlueprintNodeSpawner* Spawner)
{
    return Family + TEXT("|") + OwnerClass + TEXT("|") + MemberName + TEXT("|")
        + (Spawner != nullptr && Spawner->NodeClass != nullptr ? Spawner->NodeClass->GetPathName() : FString());
}

static TSharedRef<FJsonObject> MakeResult(
    const FString& BridgeInstanceId,
    const FString& AssetPath,
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

