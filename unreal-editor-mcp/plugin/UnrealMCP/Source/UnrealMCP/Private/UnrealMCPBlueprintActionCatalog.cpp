#include "UnrealMCPBlueprintActionCatalog.h"

#include "UnrealMCPBlueprintActionCatalogSupport.h"
#include "UnrealMCPBlueprintActionCatalogQuery.h"
#include "UnrealMCPBlueprintActionCatalogScanner.h"


FUnrealMCPBlueprintActionCatalog::FUnrealMCPBlueprintActionCatalog(
    FUnrealMCPBlueprintInspector& InInspector,
    FString InBridgeInstanceId,
    TFunction<double()> InNow,
    TFunction<double()> InScanNow)
    : Inspector(InInspector), BridgeInstanceId(MoveTemp(InBridgeInstanceId)), Now(MoveTemp(InNow)), ScanNow(MoveTemp(InScanNow))
{
}

bool FUnrealMCPBlueprintActionCatalog::ResolveForInvocation(
    const FString& ActionId,
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    const FString& AssetPath,
    const FString& GraphId,
    const FString& SnapshotId,
    FResolvedAction& OutAction,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintActionCatalogPrivate;
    check(IsInGameThread());
    OutAction = FResolvedAction();
    RemoveExpired(Now());
    const FRetainedAction* Retained = RetainedActions.Find(ActionId);
    if (!IsGuidString(ActionId, 32) || Retained == nullptr)
    {
        OutError = {TEXT("invalid_action"), TEXT("The retained action identity is unknown or expired")};
        return false;
    }
    const UEdGraphSchema* Schema = Graph != nullptr ? Graph->GetSchema() : nullptr;
    if (Blueprint == nullptr || Graph == nullptr || Schema == nullptr
        || Retained->AssetPath != AssetPath || Retained->GraphId != GraphId || Retained->SnapshotId != SnapshotId
        || Blueprint->GeneratedClass == nullptr || Blueprint->GeneratedClass->GetPathName() != Retained->TargetClass
        || Schema->GetClass()->GetPathName() != Retained->GraphSchema)
    {
        OutError = {TEXT("invalid_action"), TEXT("The retained action does not belong to this live graph snapshot")};
        return false;
    }

    UEdGraphPin* ContextPin = nullptr;
    if (!Retained->PinId.IsEmpty())
    {
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr || GuidString(Node->NodeGuid) != Retained->PinNodeId) continue;
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin != nullptr && GuidString(Pin->PinId) == Retained->PinId)
                {
                    ContextPin = Pin;
                    break;
                }
            }
            break;
        }
        if (ContextPin == nullptr)
        {
            OutError = {TEXT("invalid_action"), TEXT("The retained action pin context is no longer available")};
            return false;
        }
    }

    FBlueprintActionFilter Filter;
    Filter.Context.Blueprints.Add(Blueprint);
    Filter.Context.Graphs.Add(Graph);
    if (ContextPin != nullptr) Filter.Context.Pins.Add(ContextPin);
    FBlueprintActionDatabase& Database = FBlueprintActionDatabase::Get();
    const FBlueprintActionDatabase::FActionRegistry& RegistryActions = Database.GetAllActions();
    const double StartedAt = ScanNow();
    int32 ScannedCount = 0;
    bool bStopped = false;
    auto TryActions = [&](UObject* ActionOwner, const FBlueprintActionDatabase::FActionList& Actions)
    {
        if (ActionOwner == nullptr || bStopped) return;
        for (const UBlueprintNodeSpawner* Spawner : Actions)
        {
            if (++ScannedCount > UnrealMCP::MaxActionScan || ScanNow() - StartedAt > UnrealMCP::ActionScanSeconds)
            {
                bStopped = true;
                break;
            }
            if (Spawner == nullptr) continue;
            FBlueprintActionInfo ActionInfo(ActionOwner, Spawner);
            FString Family;
            bool bWildcard = false;
            if (!ClassifyAction(Spawner, ActionInfo, Family, bWildcard) || Filter.IsFiltered(ActionInfo)) continue;
            const UFunction* Function = ActionInfo.GetAssociatedFunction();
            const FProperty* Property = ActionInfo.GetAssociatedProperty();
            FFieldVariant MemberField = ActionInfo.GetAssociatedMemberField();
            const UClass* NodeClass = Spawner->NodeClass.Get();
            const FBlueprintActionUiSpec Ui = Spawner->GetUiSpec(Filter.Context, ActionInfo.GetBindings());
            const FString Title = Ui.MenuName.ToString().Left(256);
            const FString MemberName = Function != nullptr ? Function->GetName()
                : Property != nullptr ? Property->GetName()
                : MemberField ? MemberField.GetName()
                : Family == TEXT("flow_control") && NodeClass != nullptr && NodeClass->IsChildOf(UK2Node_MacroInstance::StaticClass())
                    ? Title
                    : NodeClass != nullptr ? NodeClass->GetName() : Title;
            const FString OwnerPath = Function != nullptr ? CanonicalOwnerPath(Function->GetOwnerClass(), Blueprint)
                : Property != nullptr ? CanonicalOwnerPath(Property->GetOwnerClass(), Blueprint)
                : CanonicalActionOwnerPath(ActionInfo, Blueprint);
            if (ActionSignature(Family, OwnerPath, MemberName, ActionOwner, Spawner) != Retained->RebuildSignature) continue;
            OutAction.Spawner = Spawner;
            OutAction.Bindings = ActionInfo.GetBindings();
            bStopped = true;
            break;
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
            TryActions(Owner, *Actions);
        }
        if (OutAction.Spawner != nullptr || bStopped) break;
    }
    if (OutAction.Spawner == nullptr && !bStopped)
    {
        for (auto It = RegistryActions.CreateConstIterator(); It; ++It)
        {
            if (ProcessedOwners.Contains(It.Key())) continue;
            TryActions(It.Key().ResolveObjectPtr(), It.Value());
            if (OutAction.Spawner != nullptr || bStopped) break;
        }
    }
    if (OutAction.Spawner == nullptr)
    {
        OutError = {TEXT("invalid_action"), TEXT("The retained action could not be re-resolved as context-valid")};
        return false;
    }
    return true;
}

void FUnrealMCPBlueprintActionCatalog::RemoveExpired(double CurrentTime)
{
    using namespace UnrealMCP::BlueprintActionCatalogPrivate;
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
    using namespace UnrealMCP::BlueprintActionCatalogPrivate;
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
    using namespace UnrealMCP::BlueprintActionCatalogPrivate;
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
    using namespace UnrealMCP::BlueprintActionCatalogPrivate;
    check(IsInGameThread());
    FActionCatalogQuery Query;
    if (!DecodeActionCatalogQuery(Arguments, Query, OutError)) return false;
    const FString& AssetPath = Query.AssetPath;
    const FString& GraphId = Query.GraphId;
    const FString& ExpectedSnapshot = Query.ExpectedSnapshot;
    const FString& Text = Query.Text;
    const FString& OwnerClassFilter = Query.OwnerClass;
    const FString& FunctionFilter = Query.Function;
    const FString& MemberFilter = Query.Member;
    const FString& FamilyFilter = Query.Family;
    const int32 Limit = Query.Limit;

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

    FActionScanResult Scan = ScanActions(Blueprint, Graph, ContextPin, Text, OwnerClassFilter,
        FunctionFilter, MemberFilter, FamilyFilter, Limit, ScanNow);
    TArray<TSharedPtr<FJsonObject>>& CandidateRecords = Scan.CandidateRecords;
    const int32 ScannedCount = Scan.ScannedCount;
    const bool bTimedOut = Scan.bTimedOut;
    const bool bScanLimited = Scan.bScanLimited;
    const bool bResultLimited = Scan.bResultLimited;
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
            AssetPath, GraphId, SnapshotId, PinNodeId, PinId, Cache.ExpiresAt});
        PublicActions.Add(MakeShared<FJsonValueObject>(Record));
    }
    Catalogs.Add(QueryKey, Cache);
    OutResult = MakeResult(BridgeInstanceId, AssetPath, GraphId, SnapshotId, PublicActions,
        ScannedCount, Cache.bTruncated, Cache.bTimedOut,
        static_cast<int32>(UnrealMCP::ActionLifetimeSeconds * 1000.0));
    return true;
}
