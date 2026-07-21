#include "UnrealMCPBlueprintInspectionBuilder.h"
#include "UnrealMCPBlueprintInspectionSupport.h"
#include "UnrealMCPBlueprintInspectionCollectors.h"
#include "UnrealMCPBlueprintInspectionFamilyCollectors.h"
#include "UnrealMCPBlueprintInspectionQuery.h"

namespace UnrealMCP::BlueprintInspectionPrivate
{

bool BuildInspection(
    const FJsonObject& Arguments,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    FString& OutSnapshot,
    bool& OutScanTruncated,
    FUnrealMCPError& OutError)
{
    OutScanTruncated = false;
    FInspectionQuery Query;
    if (!DecodeInspectionQuery(Arguments, Query, OutError)) return false;
    const FString& AssetPath = Query.AssetPath;
    const bool bIncludeInherited = Query.bIncludeInherited;
    const TSet<FString>& Sections = Query.Sections;
    const FString& GraphFilter = Query.GraphFilter;
    const FString& ComponentFilter = Query.ComponentFilter;
    const FString& MemberFilter = Query.MemberFilter;
    const FString& FunctionFilter = Query.FunctionFilter;
    const FString& LocalFilter = Query.LocalFilter;
    const FString& MacroFilter = Query.MacroFilter;
    const FString& CustomEventFilter = Query.CustomEventFilter;
    const TSet<FString>& PropertyNames = Query.PropertyNames;

    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
    if (!Asset.IsValid())
    {
        OutError = {TEXT("not_found"), TEXT("The requested asset was not found")};
        return false;
    }
    const bool bWasLoaded = Asset.IsAssetLoaded();
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
    if (Blueprint == nullptr)
    {
        OutError = {TEXT("wrong_type"), TEXT("The requested asset is not a Blueprint")};
        return false;
    }
    if (Blueprint->ParentClass == nullptr || !Blueprint->ParentClass->IsChildOf(AActor::StaticClass()))
    {
        OutError = {TEXT("wrong_type"), TEXT("The requested Blueprint is not Actor-derived")};
        return false;
    }
    UPackage* Package = Blueprint->GetOutermost();
    const bool bDirtyBefore = Package->IsDirty();
    const EBlueprintStatus StatusBefore = Blueprint->Status;
    FInspectionSink Sink(OutRecords);
    AddClassDefaultFingerprint(Blueprint, Sink.Fingerprint);

    TArray<TPair<UBlueprint*, FString>> Owners;
    if (!CollectOverviewAndComponents(Blueprint, AssetPath, bWasLoaded, bDirtyBefore, bIncludeInherited,
        ComponentFilter, PropertyNames, Sections, Sink, Owners, OutError)) return false;
    if (!CollectMembers(Blueprint, Owners, Sections, MemberFilter, Sink, OutError)) return false;
    if (!CollectFunctionsAndLocals(Blueprint, Owners, Sections, FunctionFilter, LocalFilter,
        MacroFilter, CustomEventFilter, Sink, OutError)) return false;
    if (!CollectMacros(Blueprint, Owners, Sections, FunctionFilter, LocalFilter, CustomEventFilter,
        MacroFilter, Sink, OutError)) return false;

    if (!CollectCustomEvents(Blueprint, Owners, Sections, FunctionFilter, LocalFilter, MacroFilter,
        CustomEventFilter, Sink, OutError)) return false;

    if (!CollectGraphs(Blueprint, Owners, Sections, GraphFilter, Sink, OutError)) return false;
    if (Sink.ExceedsStructuralLimit())
    {
        OutError = {TEXT("response_too_large"), TEXT("Inspection exceeds the configured structural record limit")};
        return false;
    }
    if (Package->IsDirty() != bDirtyBefore || Blueprint->Status != StatusBefore)
    {
        OutError = {TEXT("internal_error"), TEXT("Inspection unexpectedly changed Blueprint state")};
        return false;
    }
    OutSnapshot = HashLines(MoveTemp(Sink.Fingerprint));
    return true;
}
}
