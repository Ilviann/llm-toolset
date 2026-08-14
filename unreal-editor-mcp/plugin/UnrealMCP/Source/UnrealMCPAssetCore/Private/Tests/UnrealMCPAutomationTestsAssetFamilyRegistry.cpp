#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "UnrealMCPAssetFamilyRegistry.h"

namespace
{
class FSyntheticInspectionAdapter final : public IUnrealMCPAssetFamilyInspectionAdapter
{
public:
    bool Inspect(
        const FUnrealMCPAssetFamilyInspectionContext&,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPAssetFamilySelectorRouter& Selectors,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        FUnrealMCPAssetFamilyValueRecord Value;
        Value.Path = TEXT("synthetic/value");
        Value.Type = TEXT("string");
        Value.Value = MakeShared<FUnrealMCPValueString>(TEXT("inspection"));
        FUnrealMCPAssetFamilySelectorRoute Route;
        Route.Identity = TEXT("synthetic_items");
        Route.Prefix = {TEXT("items")};
        Route.bPageable = true;
        return Document.Add(MoveTemp(Value), OutError)
            && Selectors.Register(MoveTemp(Route), OutError)
            && Snapshot.Add(TEXT("synthetic"), TEXT("inspection"), OutError);
    }
};

class FSyntheticCreationAdapter final : public IUnrealMCPAssetFamilyCreationAdapter
{
public:
    bool Create(
        const FUnrealMCPAssetFamilyCreationContext& Context,
        UObject*& OutAsset,
        FUnrealMCPAssetFamilyDocumentBuilder&,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        OutAsset = Context.Outer;
        return Snapshot.Add(TEXT("synthetic"), TEXT("creation"), OutError);
    }
};

class FSyntheticEditingAdapter final : public IUnrealMCPAssetFamilyEditingAdapter
{
public:
    bool Edit(
        const FUnrealMCPAssetFamilyEditContext&,
        FUnrealMCPAssetFamilyDocumentBuilder&,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        return Snapshot.Add(TEXT("synthetic"), TEXT("editing"), OutError);
    }
};

FUnrealMCPAssetFamilyDescriptor Family(
    const TCHAR* Identity,
    UClass* NativeClass,
    EUnrealMCPAssetFamilyClassPolicy Policy,
    int32 Priority,
    bool bInspection,
    bool bCreation,
    bool bEditing)
{
    FUnrealMCPAssetFamilyDescriptor Result;
    Result.FamilyId = Identity;
    Result.NativeClass = NativeClass;
    Result.ClassPolicy = Policy;
    Result.Priority = Priority;
    Result.Capabilities = {bInspection, bCreation, bEditing};
    if (bInspection) Result.InspectionAdapter = MakeShared<FSyntheticInspectionAdapter>();
    if (bCreation) Result.CreationAdapter = MakeShared<FSyntheticCreationAdapter>();
    if (bEditing) Result.EditingAdapter = MakeShared<FSyntheticEditingAdapter>();
    Result.Limits = {{TEXT("records"), 16}};
    return Result;
}

FUnrealMCPAssetFamilyRegistry Registry()
{
    return FUnrealMCPAssetFamilyRegistry([](FName Module)
    {
        return Module != TEXT("SyntheticMissingModule");
    });
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPAssetFamilyRegistryTest,
    "UnrealMCP.AssetFamilies.RegistrySelectionCapabilitiesAndFreeze",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPAssetFamilyRegistryTest::RunTest(const FString& Parameters)
{
    FUnrealMCPError Error;
    FUnrealMCPAssetFamilySelection Selection;

    FUnrealMCPAssetFamilyRegistry Independent = Registry();
    TestTrue(TEXT("inspection-only family registers"), Independent.Register(Family(
        TEXT("inspection_only"), UObject::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact, 30,
        true, false, false), Error));
    TestTrue(TEXT("creation-only family registers"), Independent.Register(Family(
        TEXT("creation_only"), UActorComponent::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact, 30,
        false, true, false), Error));
    TestTrue(TEXT("editing-only family registers"), Independent.Register(Family(
        TEXT("editing_only"), USceneComponent::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact, 30,
        false, false, true), Error));
    TestTrue(TEXT("independent-capability registry freezes"), Independent.Freeze(Error));
    TestTrue(TEXT("inspection selects independently"), Independent.Select(
        UObject::StaticClass(), EUnrealMCPAssetFamilyCapability::Inspection, Selection, Error));
    TestEqual(TEXT("inspection family identity"), Selection.Descriptor->FamilyId, FString(TEXT("inspection_only")));
    TestFalse(TEXT("inspection does not imply creation"), Independent.Select(
        UObject::StaticClass(), EUnrealMCPAssetFamilyCapability::Creation, Selection, Error));
    TestEqual(TEXT("unsupported independent capability"), Error.Code, FString(TEXT("unsupported_operation")));
    TestTrue(TEXT("creation selects independently"), Independent.Select(
        UActorComponent::StaticClass(), EUnrealMCPAssetFamilyCapability::Creation, Selection, Error));
    TestTrue(TEXT("editing selects independently"), Independent.Select(
        USceneComponent::StaticClass(), EUnrealMCPAssetFamilyCapability::Editing, Selection, Error));
    TestFalse(TEXT("late family registration rejects"), Independent.Register(Family(
        TEXT("late"), UObject::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact, 1,
        true, false, false), Error));

    FUnrealMCPAssetFamilyRegistry Priority = Registry();
    TestTrue(TEXT("base derived family registers"), Priority.Register(Family(
        TEXT("base"), UObject::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived, 10,
        true, false, false), Error));
    TestTrue(TEXT("higher-priority derived family registers"), Priority.Register(Family(
        TEXT("component"), UActorComponent::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived, 20,
        true, false, false), Error));
    TestTrue(TEXT("exact family registers"), Priority.Register(Family(
        TEXT("scene_exact"), USceneComponent::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact, 30,
        true, false, false), Error));
    TestTrue(TEXT("priority registry freezes"), Priority.Freeze(Error));
    TestTrue(TEXT("exact class selection succeeds"), Priority.Select(
        USceneComponent::StaticClass(), EUnrealMCPAssetFamilyCapability::Inspection, Selection, Error));
    TestEqual(TEXT("exact family wins by declared priority"), Selection.Descriptor->FamilyId, FString(TEXT("scene_exact")));
    TestTrue(TEXT("derived class selection succeeds"), Priority.Select(
        UActorComponent::StaticClass(), EUnrealMCPAssetFamilyCapability::Inspection, Selection, Error));
    TestEqual(TEXT("higher-priority derived family wins"), Selection.Descriptor->FamilyId, FString(TEXT("component")));

    FUnrealMCPAssetFamilyRegistry Ambiguous = Registry();
    TestTrue(TEXT("ambiguous base registers"), Ambiguous.Register(Family(
        TEXT("ambiguous_base"), UObject::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived, 50,
        true, false, false), Error));
    TestTrue(TEXT("ambiguous component registers"), Ambiguous.Register(Family(
        TEXT("ambiguous_component"), UActorComponent::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived, 50,
        true, false, false), Error));
    TestTrue(TEXT("ambiguous registry freezes"), Ambiguous.Freeze(Error));
    TestFalse(TEXT("ambiguous classification rejects"), Ambiguous.Select(
        USceneComponent::StaticClass(), EUnrealMCPAssetFamilyCapability::Inspection, Selection, Error));
    TestEqual(TEXT("ambiguous error is stable"), Error.Code, FString(TEXT("ambiguous_classification")));

    FUnrealMCPAssetFamilyRegistry Unavailable = Registry();
    FUnrealMCPAssetFamilyDescriptor Missing = Family(
        TEXT("missing_dependency"), UObject::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact, 1,
        true, false, false);
    Missing.RequiredModules = {TEXT("SyntheticMissingModule")};
    TestTrue(TEXT("family with unavailable dependency registers"), Unavailable.Register(MoveTemp(Missing), Error));
    TestTrue(TEXT("unavailable registry freezes"), Unavailable.Freeze(Error));
    TestFalse(TEXT("unavailable family fails closed"), Unavailable.Select(
        UObject::StaticClass(), EUnrealMCPAssetFamilyCapability::Inspection, Selection, Error));
    TestEqual(TEXT("dependency error is stable"), Error.Code, FString(TEXT("dependency_unavailable")));
    TestEqual(TEXT("missing dependency is retained"), Selection.MissingModules.Num(), 1);

    FUnrealMCPAssetFamilyRegistry Collisions = Registry();
    TestTrue(TEXT("first collision fixture registers"), Collisions.Register(Family(
        TEXT("first"), UObject::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact, 1,
        true, false, false), Error));
    TestFalse(TEXT("duplicate family identity rejects"), Collisions.Register(Family(
        TEXT("first"), UActorComponent::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact, 2,
        true, false, false), Error));
    TestFalse(TEXT("classification collision rejects"), Collisions.Register(Family(
        TEXT("second"), UObject::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact, 1,
        true, false, false), Error));
    FUnrealMCPAssetFamilyDescriptor Mismatch = Family(
        TEXT("mismatch"), USceneComponent::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact, 4,
        true, false, false);
    Mismatch.InspectionAdapter.Reset();
    TestFalse(TEXT("capability disagreement rejects"), Collisions.Register(MoveTemp(Mismatch), Error));
    TestEqual(TEXT("capability disagreement error is stable"), Error.Code, FString(TEXT("capability_mismatch")));

    FUnrealMCPAssetFamilyRegistry RestartA = Registry();
    FUnrealMCPAssetFamilyRegistry RestartB = Registry();
    FUnrealMCPAssetFamilyDescriptor RestartBase = Family(
        TEXT("restart_base"), UObject::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact, 1,
        true, false, false);
    FUnrealMCPAssetFamilyDescriptor RestartComponent = Family(
        TEXT("restart_component"), UActorComponent::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact, 2,
        false, true, false);
    TestTrue(TEXT("restart A base registers"), RestartA.Register(RestartBase, Error));
    TestTrue(TEXT("restart A component registers"), RestartA.Register(RestartComponent, Error));
    TestTrue(TEXT("restart B component registers first"), RestartB.Register(RestartComponent, Error));
    TestTrue(TEXT("restart B base registers second"), RestartB.Register(RestartBase, Error));
    TestTrue(TEXT("restart A freezes"), RestartA.Freeze(Error));
    TestTrue(TEXT("restart B freezes"), RestartB.Freeze(Error));
    TestEqual(TEXT("registry fingerprint is restart deterministic"), RestartA.GetFingerprint(), RestartB.GetFingerprint());
    TestEqual(TEXT("registry ordering is stable"),
        RestartA.GetDescriptors()[0].FamilyId, RestartB.GetDescriptors()[0].FamilyId);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPAssetFamilyBuildersTest,
    "UnrealMCP.AssetFamilies.BoundedBuildersAndSyntheticAdapter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPAssetFamilyBuildersTest::RunTest(const FString& Parameters)
{
    FUnrealMCPAssetFamilyLimits Limits;
    Limits.MaxDocumentRecords = 1;
    Limits.MaxDocumentBytes = 64;
    Limits.MaxSelectorRoutes = 2;
    Limits.MaxSnapshotContributions = 2;
    FUnrealMCPAssetFamilyDocumentBuilder Document(Limits);
    FUnrealMCPAssetFamilySelectorRouter Selectors(Limits);
    FUnrealMCPAssetFamilySnapshotBuilder Snapshot(Limits);
    FUnrealMCPError Error;
    FSyntheticInspectionAdapter Adapter;
    FUnrealMCPAssetFamilyInspectionContext Context;
    TestTrue(TEXT("synthetic inspection adapter uses bounded contracts"),
        Adapter.Inspect(Context, Document, Selectors, Snapshot, Error));
    TestEqual(TEXT("semantic value record retained"), Document.GetRecords().Num(), 1);
    TestTrue(TEXT("selector routes freeze"), Selectors.Freeze(Error));
    FUnrealMCPAssetFamilySelector Selector;
    Selector.Segments = {TEXT("items"), TEXT("0")};
    const FUnrealMCPAssetFamilySelectorRoute* Route = Selectors.Resolve(Selector, Error);
    TestTrue(TEXT("longest selector prefix routes"), Route != nullptr && Route->Identity == TEXT("synthetic_items"));
    TestEqual(TEXT("snapshot is deterministic"), Snapshot.BuildSnapshotId(), Snapshot.BuildSnapshotId());

    FUnrealMCPAssetFamilyValueRecord Overflow;
    Overflow.Path = TEXT("synthetic/overflow");
    Overflow.Type = TEXT("string");
    Overflow.Value = MakeShared<FUnrealMCPValueString>(TEXT("overflow"));
    TestFalse(TEXT("document record limit rejects"), Document.Add(MoveTemp(Overflow), Error));
    TestEqual(TEXT("document bound error is stable"), Error.Code, FString(TEXT("data_limit_exceeded")));
    TestFalse(TEXT("duplicate snapshot contribution rejects"),
        Snapshot.Add(TEXT("synthetic"), TEXT("other"), Error));
    TestEqual(TEXT("snapshot collision error is stable"), Error.Code, FString(TEXT("conflict")));
    return true;
}

#endif
