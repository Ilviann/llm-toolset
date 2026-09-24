#include "UnrealMCPBlueprintInspectionBuilder.h"
#include "UnrealMCPBlueprintInspectionSupport.h"
#include "UnrealMCPBlueprintInspectionCollectors.h"
#include "UnrealMCPBlueprintInspectionFamilyCollectors.h"
#include "UnrealMCPBlueprintInspectionQuery.h"
#include "UnrealMCPWidgetTreeInspector.h"

namespace UnrealMCP::BlueprintInspectionPrivate
{

bool BuildInspection(
    const FUnrealMCPRecord& Arguments,
    const IUnrealMCPBlueprintExtensionProvider* ExtensionRegistry,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
    FString& OutSnapshot,
    FString& OutBlueprintFamily,
    TSharedPtr<FUnrealMCPRecord>& OutFamilyCapabilities,
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
    const FString& WidgetFilter = Query.WidgetFilter;
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
    UnrealMCP::BlueprintFamilyPolicy::FFamilyInfo Family =
        InspectionFamily(Blueprint);
    const UClass* ClassifiedClass = Blueprint->GeneratedClass != nullptr
        ? Blueprint->GeneratedClass : Blueprint->ParentClass;
    if (!Family.bSupported && ExtensionRegistry != nullptr
        && ExtensionRegistry->ClassifyBlueprintClass(
            ClassifiedClass, Family.Name, Family.NativeBaseClass))
    {
        Family.bSupported = true;
    }
    if (!Family.bSupported)
    {
        OutError = {TEXT("wrong_type"), TEXT("The requested Blueprint does not belong to a published authoring family")};
        return false;
    }
    OutBlueprintFamily = Family.Name;
    OutFamilyCapabilities = UnrealMCP::BlueprintFamilyPolicy::BuildLiveCapabilities(Blueprint, Family);
    if (Family.Name == TEXT("animation") || Family.Name == TEXT("interface")
        || Family.Name == TEXT("function_library") || Family.Name == TEXT("macro_library"))
    {
        OutFamilyCapabilities = MakeShared<FUnrealMCPRecord>();
        OutFamilyCapabilities->SetBoolField(TEXT("inspection_only"), true);
        OutFamilyCapabilities->SetBoolField(TEXT("inspect"), true);
        OutFamilyCapabilities->SetBoolField(TEXT("authoring"), false);
    }
    UPackage* Package = Blueprint->GetOutermost();
    const bool bDirtyBefore = Package->IsDirty();
    const EBlueprintStatus StatusBefore = Blueprint->Status;
    FInspectionSink Sink(OutRecords);
    // Bound the entire requested scope before declaration collectors scan graph nodes.
    int32 InternalWork = 0;
    TSet<UBlueprint*> VisitedOwners;
    for (UBlueprint* Owner = Blueprint; Owner != nullptr; )
    {
        if (VisitedOwners.Contains(Owner)) break;
        VisitedOwners.Add(Owner);
        TArray<UEdGraph*> Graphs;
        if (!UnrealMCP::InspectionBudget::CollectGraphs(Owner, Graphs, InternalWork, OutError)) return false;
        Owner = bIncludeInherited ? UBlueprint::GetBlueprintFromClass(Owner->ParentClass) : nullptr;
    }
    if (!Query.GraphName.IsEmpty())
    {
        int32 Matches = 0;
        for (UBlueprint* Owner : VisitedOwners)
        {
            TArray<UEdGraph*> Graphs;
            int32 Work = 0;
            if (!UnrealMCP::InspectionBudget::CollectGraphs(Owner, Graphs, Work, OutError)) return false;
            for (UEdGraph* Graph : Graphs) if (Graph->GetName() == Query.GraphName)
            { Query.GraphFilter = GuidString(Graph->GraphGuid); ++Matches; }
        }
        if (Matches != 1 || Query.GraphFilter.IsEmpty())
        { OutError = {TEXT("invalid_argument"), TEXT("graph_name must resolve exactly one stable graph")}; return false; }
    }
    if (!Query.ComponentName.IsEmpty())
    {
        int32 Matches = 0;
        for (USCS_Node* Node : InspectionComponentNodes(Blueprint))
            if (Node->GetVariableName().ToString() == Query.ComponentName)
            { Query.ComponentFilter = GuidString(Node->VariableGuid); ++Matches; }
        if (Matches != 1 || Query.ComponentFilter.IsEmpty())
        { OutError = {TEXT("invalid_argument"), TEXT("component_name must resolve exactly one stable component")}; return false; }
    }
    AddClassDefaultFingerprint(Blueprint, Sink.Fingerprint);
    if (!Sink.CheckLimits(OutError)) return false;

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
    if (!CollectWidgetTree(Blueprint, WidgetFilter, PropertyNames, Sections, Sink, OutError)) return false;
    if (ExtensionRegistry != nullptr
        && !ExtensionRegistry->AppendBlueprintInspection(
            *Blueprint, MakeShared<FUnrealMCPRecord>(Arguments), OutRecords, Sink.Fingerprint,
            OutFamilyCapabilities, OutError))
    {
        return false;
    }
    if (!Sink.CheckLimits(OutError)) return false;
    for (const auto& RecordValue : OutRecords)
    {
        if (!RecordValue.IsValid()) continue;
        const auto Record = RecordValue->AsObject();
        if (Record.IsValid() && OutFamilyCapabilities->HasField(TEXT("inspection_only")))
        {
            if (Record->HasField(TEXT("editable"))) Record->SetBoolField(TEXT("editable"), false);
            Record->RemoveField(TEXT("replacement_boundary"));
        }
        if (Record.IsValid() && Record->GetStringField(TEXT("section")) == TEXT("summary"))
        {
            Record->SetStringField(TEXT("blueprint_family"), OutBlueprintFamily);
            Record->SetObjectField(TEXT("family_capabilities"), OutFamilyCapabilities.ToSharedRef());
        }
    }
    if (!Sink.CheckLimits(OutError)) return false;
    if (Package->IsDirty() != bDirtyBefore || Blueprint->Status != StatusBefore)
    {
        OutError = {TEXT("internal_error"), TEXT("Inspection unexpectedly changed Blueprint state")};
        return false;
    }
    OutSnapshot = HashLines(MoveTemp(Sink.Fingerprint));
    return true;
}
}
