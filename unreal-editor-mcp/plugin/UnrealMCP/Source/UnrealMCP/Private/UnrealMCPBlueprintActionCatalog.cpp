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

namespace
{
bool HasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed)
{
    TSet<FString> Names;
    for (const TCHAR* Name : Allowed) Names.Add(Name);
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
        if (!Names.Contains(Pair.Key)) return false;
    return true;
}

FString GuidString(const FGuid& Guid)
{
    return Guid.IsValid() ? Guid.ToString(EGuidFormats::Digits).ToLower() : FString();
}

bool IsGuidString(const FString& Value, int32 Digits)
{
    if (Value.Len() != Digits) return false;
    for (TCHAR Character : Value)
        if (!FChar::IsHexDigit(Character) || FChar::IsUpper(Character)) return false;
    return true;
}

bool NormalizeAssetPath(const FString& Input, FString& OutObjectPath)
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

bool ReadOptionalString(
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

FString CanonicalOwnerPath(const UClass* OwnerClass, const UBlueprint* Blueprint)
{
    if (OwnerClass == nullptr) return FString();
    if (Blueprint != nullptr && OwnerClass == Blueprint->SkeletonGeneratedClass && Blueprint->GeneratedClass != nullptr)
        return Blueprint->GeneratedClass->GetPathName();
    return OwnerClass->GetPathName();
}

FString QueryDigest(const FString& Material)
{
    FTCHARToUTF8 Encoded(*Material);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

bool IsCoreFamily(const UBlueprintNodeSpawner* Spawner, FString& OutFamily)
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

FString ActionSignature(
    const FString& Family,
    const FString& OwnerClass,
    const FString& MemberName,
    const UBlueprintNodeSpawner* Spawner)
{
    return Family + TEXT("|") + OwnerClass + TEXT("|") + MemberName + TEXT("|")
        + (Spawner != nullptr && Spawner->NodeClass != nullptr ? Spawner->NodeClass->GetPathName() : FString());
}

TSharedRef<FJsonObject> MakeResult(
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

FUnrealMCPBlueprintActionCatalog::FUnrealMCPBlueprintActionCatalog(
    FUnrealMCPBlueprintInspector& InInspector,
    FString InBridgeInstanceId,
    TFunction<double()> InNow,
    TFunction<double()> InScanNow)
    : Inspector(InInspector), BridgeInstanceId(MoveTemp(InBridgeInstanceId)), Now(MoveTemp(InNow)), ScanNow(MoveTemp(InScanNow))
{
}

void FUnrealMCPBlueprintActionCatalog::RemoveExpired(double CurrentTime)
{
    for (auto It = RetainedActions.CreateIterator(); It; ++It)
        if (It.Value().ExpiresAt <= CurrentTime) It.RemoveCurrent();
    for (auto It = Catalogs.CreateIterator(); It; ++It)
    {
        bool bMissing = It.Value().ExpiresAt <= CurrentTime;
        for (const FString& Id : It.Value().ActionIds) bMissing |= !RetainedActions.Contains(Id);
        if (bMissing) It.RemoveCurrent();
    }
}

void FUnrealMCPBlueprintActionCatalog::EvictFor(int32 IncomingCount)
{
    while (!RetainedActions.IsEmpty() && RetainedActions.Num() + IncomingCount > UnrealMCP::MaxRetainedActions)
    {
        FString OldestId;
        double OldestExpiry = TNumericLimits<double>::Max();
        for (const TPair<FString, FRetainedAction>& Pair : RetainedActions)
            if (Pair.Value.ExpiresAt < OldestExpiry) { OldestExpiry = Pair.Value.ExpiresAt; OldestId = Pair.Key; }
        RetainedActions.Remove(OldestId);
    }
    for (auto It = Catalogs.CreateIterator(); It; ++It)
    {
        bool bMissing = false;
        for (const FString& Id : It.Value().ActionIds) bMissing |= !RetainedActions.Contains(Id);
        if (bMissing)
        {
            for (const FString& Id : It.Value().ActionIds) RetainedActions.Remove(Id);
            It.RemoveCurrent();
        }
    }
}

bool FUnrealMCPBlueprintActionCatalog::BuildCachedResult(
    const FCachedCatalog& Cache,
    TSharedPtr<FJsonObject>& OutResult) const
{
    TArray<TSharedPtr<FJsonValue>> Actions;
    for (const FString& Id : Cache.ActionIds)
    {
        const FRetainedAction* Retained = RetainedActions.Find(Id);
        if (Retained == nullptr || !Retained->PublicRecord.IsValid()) return false;
        Actions.Add(MakeShared<FJsonValueObject>(Retained->PublicRecord));
    }
    OutResult = MakeResult(BridgeInstanceId, Cache.AssetPath, Cache.GraphId, Cache.SnapshotId,
        Actions, Cache.ScannedCount, Cache.bTruncated, Cache.bTimedOut,
        FMath::Max(0, static_cast<int32>((Cache.ExpiresAt - Now()) * 1000.0)));
    return true;
}

bool FUnrealMCPBlueprintActionCatalog::Execute(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (!Arguments.IsValid() || !HasOnlyFields(*Arguments, {TEXT("asset_path"), TEXT("graph_id"), TEXT("expected_snapshot"),
        TEXT("text"), TEXT("owner_class"), TEXT("function"), TEXT("member"), TEXT("node_family"), TEXT("pin_context"), TEXT("limit")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Action catalog arguments have an invalid shape")};
        return false;
    }
    FString RawAssetPath;
    FString AssetPath;
    FString GraphId;
    FString ExpectedSnapshot;
    if (!Arguments->TryGetStringField(TEXT("asset_path"), RawAssetPath) || !NormalizeAssetPath(RawAssetPath, AssetPath)
        || !Arguments->TryGetStringField(TEXT("graph_id"), GraphId) || !IsGuidString(GraphId, 32)
        || !Arguments->TryGetStringField(TEXT("expected_snapshot"), ExpectedSnapshot) || !IsGuidString(ExpectedSnapshot, 40))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_path, graph_id, or expected_snapshot is invalid")};
        return false;
    }
    FString Text;
    FString OwnerClassFilter;
    FString FunctionFilter;
    FString MemberFilter;
    FString FamilyFilter;
    if (!ReadOptionalString(*Arguments, TEXT("text"), 128, Text, OutError)
        || !ReadOptionalString(*Arguments, TEXT("owner_class"), 512, OwnerClassFilter, OutError)
        || !ReadOptionalString(*Arguments, TEXT("function"), 128, FunctionFilter, OutError)
        || !ReadOptionalString(*Arguments, TEXT("member"), 128, MemberFilter, OutError)
        || !ReadOptionalString(*Arguments, TEXT("node_family"), 32, FamilyFilter, OutError)) return false;
    if (!OwnerClassFilter.IsEmpty() && (!OwnerClassFilter.StartsWith(TEXT("/")) || OwnerClassFilter.Contains(TEXT("..")) || OwnerClassFilter.Contains(TEXT("\\"))))
    {
        OutError = {TEXT("invalid_argument"), TEXT("owner_class must be an exact class path")};
        return false;
    }
    if ((!FunctionFilter.IsEmpty() && !MemberFilter.IsEmpty())
        || (!FunctionFilter.IsEmpty() && !FamilyFilter.IsEmpty() && FamilyFilter != TEXT("function_call"))
        || (!MemberFilter.IsEmpty() && FamilyFilter == TEXT("function_call"))
        || (!FamilyFilter.IsEmpty() && FamilyFilter != TEXT("function_call") && FamilyFilter != TEXT("variable_get") && FamilyFilter != TEXT("variable_set")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Function, member, and node-family filters conflict")};
        return false;
    }
    int32 Limit = UnrealMCP::DefaultActionResults;
    if (Arguments->HasField(TEXT("limit")))
    {
        double Number = 0.0;
        if (!Arguments->TryGetNumberField(TEXT("limit"), Number) || !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
            || Number < 1 || Number > UnrealMCP::MaxActionResults)
        {
            OutError = {TEXT("invalid_argument"), TEXT("limit is outside the supported range")};
            return false;
        }
        Limit = static_cast<int32>(Number);
    }

    const TSharedRef<FJsonObject> InspectArguments = MakeShared<FJsonObject>();
    InspectArguments->SetStringField(TEXT("mode"), TEXT("inspect"));
    InspectArguments->SetStringField(TEXT("asset_path"), AssetPath);
    InspectArguments->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("summary"))});
    InspectArguments->SetNumberField(TEXT("page_size"), 1);
    TSharedPtr<FJsonObject> Inspection;
    if (!Inspector.Execute(InspectArguments, Inspection, OutError)) return false;
    const FString SnapshotId = Inspection->GetStringField(TEXT("snapshot_id"));
    if (SnapshotId != ExpectedSnapshot)
    {
        OutError = {TEXT("stale_precondition"), TEXT("The Blueprint structural snapshot changed before cataloging")};
        return false;
    }

    FString QueryMaterial = BridgeInstanceId + TEXT("|") + AssetPath + TEXT("|") + GraphId + TEXT("|") + SnapshotId
        + TEXT("|") + Text.ToLower() + TEXT("|") + OwnerClassFilter + TEXT("|") + FunctionFilter.ToLower()
        + TEXT("|") + MemberFilter.ToLower() + TEXT("|") + FamilyFilter + TEXT("|") + LexToString(Limit);
    FString PinNodeId;
    FString PinId;
    const TSharedPtr<FJsonObject>* PinContextObject = nullptr;
    const bool bHasPinContext = Arguments->HasField(TEXT("pin_context"));
    if (Arguments->TryGetObjectField(TEXT("pin_context"), PinContextObject))
    {
        if (PinContextObject == nullptr || !PinContextObject->IsValid()
            || !HasOnlyFields(**PinContextObject, {TEXT("node_id"), TEXT("pin_id")})
            || !(*PinContextObject)->TryGetStringField(TEXT("node_id"), PinNodeId) || !IsGuidString(PinNodeId, 32)
            || !(*PinContextObject)->TryGetStringField(TEXT("pin_id"), PinId) || !IsGuidString(PinId, 32))
        {
            OutError = {TEXT("invalid_argument"), TEXT("pin_context must identify one exact node and pin")};
            return false;
        }
    }
    else if (bHasPinContext)
    {
        OutError = {TEXT("invalid_argument"), TEXT("pin_context must be an object")};
        return false;
    }
    FString QueryKey;
    const double CurrentTime = Now();
    RemoveExpired(CurrentTime);

    IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
    if (Blueprint == nullptr || Blueprint->GeneratedClass == nullptr || !Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
    {
        OutError = {TEXT("not_found"), TEXT("The requested Actor Blueprint was not found")};
        return false;
    }
    UPackage* Package = Blueprint->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();
    const EBlueprintStatus StatusBefore = Blueprint->Status;
    const int32 SelectedObjectsBefore = GEditor != nullptr && GEditor->GetSelectedObjects() != nullptr
        ? GEditor->GetSelectedObjects()->Num() : 0;
    const int32 SelectedActorsBefore = GEditor != nullptr && GEditor->GetSelectedActors() != nullptr
        ? GEditor->GetSelectedActors()->Num() : 0;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    UEdGraph* Graph = nullptr;
    for (UEdGraph* Candidate : Graphs)
        if (Candidate != nullptr && GuidString(Candidate->GraphGuid) == GraphId) { Graph = Candidate; break; }
    if (Graph == nullptr)
    {
        OutError = {TEXT("not_found"), TEXT("The requested graph identity was not found")};
        return false;
    }
    const UEdGraphSchema* GraphSchema = Graph->GetSchema();
    if (GraphSchema == nullptr || !GraphSchema->IsA<UEdGraphSchema_K2>())
    {
        OutError = {TEXT("invalid_argument"), TEXT("The requested graph does not use the K2 Blueprint schema")};
        return false;
    }
    QueryMaterial += TEXT("|") + (Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetPathName() : FString())
        + TEXT("|") + GraphSchema->GetClass()->GetPathName();
    UEdGraphPin* ContextPin = nullptr;
    if (!PinId.IsEmpty())
    {
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr || GuidString(Node->NodeGuid) != PinNodeId) continue;
            for (UEdGraphPin* Pin : Node->Pins)
                if (Pin != nullptr && GuidString(Pin->PinId) == PinId) { ContextPin = Pin; break; }
        }
        if (ContextPin == nullptr)
        {
            OutError = {TEXT("not_found"), TEXT("The requested pin context was not found in the graph")};
            return false;
        }
    }
    QueryKey = QueryDigest(QueryMaterial + TEXT("|") + PinNodeId + TEXT("|") + PinId);
    if (const FCachedCatalog* Cache = Catalogs.Find(QueryKey))
    {
        if (BuildCachedResult(*Cache, OutResult))
        {
            if ((Package != nullptr && Package->IsDirty() != bDirtyBefore) || Blueprint->Status != StatusBefore
                || (GEditor != nullptr && GEditor->GetSelectedObjects() != nullptr && GEditor->GetSelectedObjects()->Num() != SelectedObjectsBefore)
                || (GEditor != nullptr && GEditor->GetSelectedActors() != nullptr && GEditor->GetSelectedActors()->Num() != SelectedActorsBefore))
            {
                OutError = {TEXT("internal_error"), TEXT("Cached action catalog unexpectedly changed Blueprint state")};
                return false;
            }
            return true;
        }
    }

    FBlueprintActionFilter Filter;
    Filter.Context.Blueprints.Add(Blueprint);
    Filter.Context.Graphs.Add(Graph);
    if (ContextPin != nullptr) Filter.Context.Pins.Add(ContextPin);
    const double StartedAt = ScanNow();
    FBlueprintActionDatabase& Database = FBlueprintActionDatabase::Get();
    const FBlueprintActionDatabase::FActionRegistry& RegistryActions = Database.GetAllActions();
    TArray<TSharedPtr<FJsonObject>> CandidateRecords;
    TSet<FString> Signatures;
    int32 ScannedCount = 0;
    bool bTimedOut = false;
    bool bScanLimited = false;
    auto ProcessActions = [&](UObject* ActionOwner, const FBlueprintActionDatabase::FActionList& ActionList)
    {
        if (ActionOwner == nullptr) return;
        for (const UBlueprintNodeSpawner* Spawner : ActionList)
        {
            if (++ScannedCount > UnrealMCP::MaxActionScan) { bScanLimited = true; break; }
            if (ScanNow() - StartedAt > UnrealMCP::ActionScanSeconds) { bTimedOut = true; break; }
            FString Family;
            if (Spawner == nullptr || !IsCoreFamily(Spawner, Family) || (!FamilyFilter.IsEmpty() && Family != FamilyFilter)) continue;
            FBlueprintActionInfo ActionInfo(ActionOwner, Spawner);
            if (Filter.IsFiltered(ActionInfo)) continue;
            const UFunction* Function = ActionInfo.GetAssociatedFunction();
            const FProperty* Property = ActionInfo.GetAssociatedProperty();
            if (Family == TEXT("function_call") && Function == nullptr) continue;
            if (Family != TEXT("function_call") && Property == nullptr) continue;
            const FString MemberName = Function != nullptr ? Function->GetName() : Property->GetName();
            const UClass* OwnerClass = Function != nullptr ? Function->GetOwnerClass() : Property->GetOwnerClass();
            const FString OwnerPath = CanonicalOwnerPath(OwnerClass, Blueprint);
            if (!OwnerClassFilter.IsEmpty() && OwnerPath != OwnerClassFilter) continue;
            if (!FunctionFilter.IsEmpty() && (Function == nullptr || !MemberName.Equals(FunctionFilter, ESearchCase::IgnoreCase))) continue;
            if (!MemberFilter.IsEmpty() && (Property == nullptr || !MemberName.Equals(MemberFilter, ESearchCase::IgnoreCase))) continue;
            const FBlueprintActionUiSpec Ui = Spawner->GetUiSpec(Filter.Context, ActionInfo.GetBindings());
            const FString Title = Ui.MenuName.ToString().Left(256);
            if (!Text.IsEmpty() && !Title.Equals(Text, ESearchCase::IgnoreCase) && !MemberName.Equals(Text, ESearchCase::IgnoreCase)) continue;
            const FString Signature = ActionSignature(Family, OwnerPath, MemberName, Spawner);
            if (Signatures.Contains(Signature)) continue;
            Signatures.Add(Signature);
            const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("_rebuild_signature"), Signature);
            Record->SetStringField(TEXT("node_family"), Family);
            Record->SetStringField(TEXT("title"), Title);
            Record->SetStringField(TEXT("category"), Ui.Category.ToString().Left(256));
            Record->SetStringField(TEXT("owner_class"), OwnerPath);
            Record->SetStringField(TEXT("member_name"), MemberName);
            Record->SetStringField(TEXT("member_kind"), Function != nullptr ? TEXT("function") : TEXT("variable"));
            if (Function != nullptr)
            {
                Record->SetBoolField(TEXT("pure"), Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
                Record->SetBoolField(TEXT("static"), Function->HasAnyFunctionFlags(FUNC_Static));
                Record->SetBoolField(TEXT("const"), Function->HasAnyFunctionFlags(FUNC_Const));
            }
            CandidateRecords.Add(Record);
        }
    };
    TSet<FObjectKey> ProcessedOwners;
    TArray<UObject*> PriorityOwners = {Blueprint, Blueprint->SkeletonGeneratedClass, Blueprint->GeneratedClass};
    for (UClass* Class = Blueprint->GeneratedClass; Class != nullptr; Class = Class->GetSuperClass()) PriorityOwners.AddUnique(Class);
    for (UClass* Class = Blueprint->SkeletonGeneratedClass; Class != nullptr; Class = Class->GetSuperClass()) PriorityOwners.AddUnique(Class);
    for (UObject* Owner : PriorityOwners)
    {
        if (Owner == nullptr) continue;
        const FObjectKey Key(Owner);
        if (const FBlueprintActionDatabase::FActionList* Actions = RegistryActions.Find(Key))
        {
            ProcessedOwners.Add(Key);
            ProcessActions(Owner, *Actions);
        }
        if (bScanLimited || bTimedOut) break;
    }
    if (!bScanLimited && !bTimedOut)
    {
        for (auto It = RegistryActions.CreateConstIterator(); It; ++It)
        {
            if (ProcessedOwners.Contains(It.Key())) continue;
            ProcessActions(It.Key().ResolveObjectPtr(), It.Value());
            if (bScanLimited || bTimedOut) break;
        }
    }
    CandidateRecords.Sort([](const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right)
    {
        const FString A = Left->GetStringField(TEXT("node_family")) + TEXT("|") + Left->GetStringField(TEXT("owner_class"))
            + TEXT("|") + Left->GetStringField(TEXT("member_name")) + TEXT("|") + Left->GetStringField(TEXT("title"));
        const FString B = Right->GetStringField(TEXT("node_family")) + TEXT("|") + Right->GetStringField(TEXT("owner_class"))
            + TEXT("|") + Right->GetStringField(TEXT("member_name")) + TEXT("|") + Right->GetStringField(TEXT("title"));
        return A < B;
    });
    const bool bResultLimited = CandidateRecords.Num() > Limit;
    if (CandidateRecords.Num() > Limit) CandidateRecords.SetNum(Limit);
    if ((Package != nullptr && Package->IsDirty() != bDirtyBefore) || Blueprint->Status != StatusBefore
        || (GEditor != nullptr && GEditor->GetSelectedObjects() != nullptr && GEditor->GetSelectedObjects()->Num() != SelectedObjectsBefore)
        || (GEditor != nullptr && GEditor->GetSelectedActors() != nullptr && GEditor->GetSelectedActors()->Num() != SelectedActorsBefore))
    {
        OutError = {TEXT("internal_error"), TEXT("Action catalog unexpectedly changed Blueprint state")};
        return false;
    }

    RemoveExpired(Now());
    EvictFor(CandidateRecords.Num());
    if (!Catalogs.Contains(QueryKey) && Catalogs.Num() >= UnrealMCP::MaxRetainedCatalogs)
    {
        FString OldestKey;
        double OldestExpiry = TNumericLimits<double>::Max();
        for (const TPair<FString, FCachedCatalog>& Pair : Catalogs)
            if (Pair.Value.ExpiresAt < OldestExpiry) { OldestExpiry = Pair.Value.ExpiresAt; OldestKey = Pair.Key; }
        if (const FCachedCatalog* Oldest = Catalogs.Find(OldestKey))
            for (const FString& Id : Oldest->ActionIds) RetainedActions.Remove(Id);
        Catalogs.Remove(OldestKey);
    }
    FCachedCatalog Cache;
    Cache.AssetPath = AssetPath;
    Cache.GraphId = GraphId;
    Cache.SnapshotId = SnapshotId;
    Cache.ScannedCount = ScannedCount;
    Cache.bTruncated = bScanLimited || bTimedOut || bResultLimited;
    Cache.bTimedOut = bTimedOut;
    Cache.ExpiresAt = Now() + UnrealMCP::ActionLifetimeSeconds;
    TArray<TSharedPtr<FJsonValue>> PublicActions;
    for (const TSharedPtr<FJsonObject>& Record : CandidateRecords)
    {
        const FString ActionId = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        const FString RebuildSignature = Record->GetStringField(TEXT("_rebuild_signature"));
        Record->RemoveField(TEXT("_rebuild_signature"));
        Record->SetStringField(TEXT("action_id"), ActionId);
        Cache.ActionIds.Add(ActionId);
        RetainedActions.Add(ActionId, FRetainedAction{Record, QueryKey, RebuildSignature,
            Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetPathName() : FString(),
            GraphSchema->GetClass()->GetPathName(),
            AssetPath, GraphId, SnapshotId, Cache.ExpiresAt});
        PublicActions.Add(MakeShared<FJsonValueObject>(Record));
    }
    Catalogs.Add(QueryKey, Cache);
    OutResult = MakeResult(BridgeInstanceId, AssetPath, GraphId, SnapshotId, PublicActions,
        ScannedCount, Cache.bTruncated, Cache.bTimedOut,
        static_cast<int32>(UnrealMCP::ActionLifetimeSeconds * 1000.0));
    return true;
}
