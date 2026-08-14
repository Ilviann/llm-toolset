#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAssetFamilyConformance.h"
#include "UnrealMCPAssetInspectionAdapters.h"

#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"

namespace
{
using namespace UnrealMCP::Tests::AssetFamilyConformance;

class FSyntheticAdapter final
    : public IUnrealMCPAssetFamilyInspectionAdapter
    , public IUnrealMCPAssetFamilyCreationAdapter
    , public IUnrealMCPAssetFamilyEditingAdapter
{
public:
    virtual bool Inspect(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPAssetFamilySelectorRouter& Selectors,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        FUnrealMCPAssetFamilyValueRecord Record;
        Record.Path = TEXT("identity/path");
        Record.Type = TEXT("string");
        Record.Value = MakeShared<FUnrealMCPValueString>(Context.Identity.ObjectPath);
        return Document.Add(MoveTemp(Record), OutError)
            && Selectors.Register({TEXT("items"), {TEXT("items")}, true, false}, OutError)
            && Snapshot.Add(TEXT("identity"), Context.Identity.ObjectPath, OutError)
            && Snapshot.Add(TEXT("page"),
                FString::Printf(TEXT("%d:%d"), Context.PageIndex, Context.PageSize), OutError);
    }

    virtual bool Create(
        const FUnrealMCPAssetFamilyCreationContext& Context,
        UObject*& OutAsset,
        FUnrealMCPAssetFamilyDocumentBuilder& ReadBack,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        OutAsset = Context.Outer;
        return Snapshot.Add(TEXT("creation"), Context.CanonicalObjectPath, OutError);
    }

    virtual bool Edit(
        const FUnrealMCPAssetFamilyEditContext& Context,
        FUnrealMCPAssetFamilyDocumentBuilder& ReadBack,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        return Snapshot.Add(TEXT("editing"), Context.Operation, OutError);
    }
};

FUnrealMCPAssetFamilyDescriptor SyntheticDescriptor(
    const FString& FamilyId,
    FUnrealMCPAssetFamilyCapabilities Capabilities,
    bool bMissingDependency = false)
{
    const TSharedRef<FSyntheticAdapter> Adapter = MakeShared<FSyntheticAdapter>();
    FUnrealMCPAssetFamilyDescriptor Descriptor;
    Descriptor.FamilyId = FamilyId;
    Descriptor.NativeClass = UObject::StaticClass();
    Descriptor.ClassPolicy = EUnrealMCPAssetFamilyClassPolicy::Exact;
    Descriptor.Priority = 100;
    Descriptor.Capabilities = Capabilities;
    Descriptor.Bounds.MaxDocumentRecords = 2;
    Descriptor.Bounds.MaxSelectorRoutes = 2;
    Descriptor.Bounds.MaxSnapshotContributions = 4;
    Descriptor.Limits = {{TEXT("records"), 2}, {TEXT("selectors"), 2}};
    if (Capabilities.bInspection) Descriptor.InspectionAdapter = Adapter;
    if (Capabilities.bCreation) Descriptor.CreationAdapter = Adapter;
    if (Capabilities.bEditing) Descriptor.EditingAdapter = Adapter;
    if (bMissingDependency)
    {
        Descriptor.RequiredModules = {TEXT("UnrealMCPConformanceMissing")};
    }
    return Descriptor;
}

FFixture Fixture(
    const TCHAR* FamilyId,
    FUnrealMCPAssetFamilyCapabilities Capabilities,
    bool bDependencyAvailable = true)
{
    FFixture Result;
    Result.Label = FamilyId;
    Result.FamilyId = FamilyId;
    Result.TargetClass = UObject::StaticClass();
    Result.Capabilities = Capabilities;
    Result.bDependencyAvailable = bDependencyAvailable;
    Result.MakeDescriptor = [Identity = FString(FamilyId), Capabilities, bDependencyAvailable]()
    {
        return SyntheticDescriptor(Identity, Capabilities, !bDependencyAvailable);
    };
    return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPAssetFamilyConformanceTest,
    "UnrealMCP.AssetFamilies.ConformanceMatrix",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPAssetFamilyConformanceTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests::AssetFamilyConformance;
    const TArray<FFixture> Fixtures = {
        Fixture(TEXT("inspection_only"), {true, false, false}),
        Fixture(TEXT("creation_only"), {false, true, false}),
        Fixture(TEXT("editing_only"), {false, false, true}),
        Fixture(TEXT("combined"), {true, true, true}),
        Fixture(TEXT("missing_dependency"), {true, true, true}, false),
    };
    for (const FFixture& Entry : Fixtures)
    {
        if (!RunFixture(*this, Entry)) return false;
    }

    FUnrealMCPAssetFamilyRegistry BuiltIns;
    FUnrealMCPError Error;
    if (!TestTrue(TEXT("built-in family adapters register"),
            UnrealMCP::AssetInspection::RegisterBuiltInAdapters(BuiltIns, Error))
        || !TestTrue(TEXT("built-in family adapters freeze"), BuiltIns.Freeze(Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    VerifyRegisteredFamily(
        *this, BuiltIns, TEXT("core_blueprint"), UBlueprint::StaticClass(),
        {true, false, false});
    VerifyRegisteredFamily(
        *this, BuiltIns, TEXT("neutral_asset"), UObject::StaticClass(),
        {true, false, false});
    FAuthoringFixture GameData;
    GameData.FamilyId = TEXT("game_data");
    GameData.CreateAsset = [](UPackage* Package, FName Name)
    {
        UDataTable* Table = NewObject<UDataTable>(
            Package, Name, RF_Public | RF_Standalone | RF_Transactional);
        Table->RowStruct = FTableRowBase::StaticStruct();
        return Table;
    };
    GameData.ReadValue = [](UObject* Asset)
    {
        return CastChecked<UDataTable>(Asset)->ImportKeyField;
    };
    GameData.WriteValue = [](UObject* Asset, const FString& Value)
    {
        UDataTable* Table = CastChecked<UDataTable>(Asset);
        Table->Modify();
        Table->ImportKeyField = Value;
    };
    GameData.ReadUnrelatedValue = [](UObject* Asset)
    {
        const UDataTable* Table = CastChecked<UDataTable>(Asset);
        return Table->RowStruct != nullptr ? Table->RowStruct->GetPathName() : FString();
    };
    GameData.Retire = [](UPackage* Package, UObject* Asset)
    {
        Asset->ClearFlags(RF_Public | RF_Standalone);
        Asset->Rename(nullptr, GetTransientPackage(),
            REN_DontCreateRedirectors | REN_NonTransactional);
        Asset->MarkAsGarbage();
        Package->SetDirtyFlag(false);
        Package->Rename(
            *(TEXT("/Temp/UnrealMCPConformance_")
                + FGuid::NewGuid().ToString(EGuidFormats::Digits)),
            nullptr,
            REN_DontCreateRedirectors | REN_NonTransactional);
        Package->MarkAsGarbage();
    };
    if (!RunAuthoringFixture(*this, GameData)) return false;
    return true;
}

#endif
