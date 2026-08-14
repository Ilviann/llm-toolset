#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "Misc/SecureHash.h"
#include "UnrealMCPAssetAuthoringKernel.h"
#include "UnrealMCPAssetFamilyRegistry.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace UnrealMCP::Tests::AssetFamilyConformance
{
struct FFixture
{
    FString Label;
    FString FamilyId;
    UClass* TargetClass = nullptr;
    FUnrealMCPAssetFamilyCapabilities Capabilities;
    TFunction<FUnrealMCPAssetFamilyDescriptor()> MakeDescriptor;
    bool bDependencyAvailable = true;
    bool bExerciseAdapters = true;
};

struct FAuthoringFixture
{
    FString FamilyId;
    TFunction<UObject*(UPackage*, FName)> CreateAsset;
    TFunction<FString(UObject*)> ReadValue;
    TFunction<void(UObject*, const FString&)> WriteValue;
    TFunction<FString(UObject*)> ReadUnrelatedValue;
    TFunction<void(UPackage*, UObject*)> Retire;
};

inline FString SnapshotFor(const FString& Value)
{
    FTCHARToUTF8 Encoded(*Value);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

inline bool RunAuthoringFixture(FAutomationTestBase& Test, const FAuthoringFixture& Fixture)
{
    const FString Root = TEXT("/Game/UnrealMCPConformance/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString FailedPackageName = Root + TEXT("/Failed");
    const FString FailedObjectPath =
        FUnrealMCPAssetAuthoringKernel::ObjectPathForPackage(FailedPackageName);
    FUnrealMCPAssetCreationHooks FailedHooks;
    FailedHooks.Create = [&Fixture](UPackage* Package, UObject*& OutAsset, FUnrealMCPError&)
    {
        OutAsset = Fixture.CreateAsset(Package, TEXT("Failed"));
        return OutAsset != nullptr;
    };
    FailedHooks.Finalize = [](UObject*, FUnrealMCPError& OutError)
    {
        OutError = {TEXT("compile_failed"), TEXT("Synthetic conformance failure")};
        return false;
    };
    FailedHooks.Persist = [](UObject*, FUnrealMCPError&) { return true; };
    FailedHooks.ReadBack = [](UObject*, FString&, FUnrealMCPError&) { return false; };
    FUnrealMCPAssetCreationResult CreationResult;
    FUnrealMCPError Error;
    Test.TestFalse(*(Fixture.FamilyId + TEXT(" failed creation rejects")),
        FUnrealMCPAssetAuthoringKernel::ExecuteCreation(
            {FString(), FailedPackageName, FailedObjectPath},
            FailedHooks, CreationResult, Error));
    Test.TestEqual(*(Fixture.FamilyId + TEXT(" creation cleanup error")),
        Error.Code, FString(TEXT("compile_failed")));
    Test.TestNull(*(Fixture.FamilyId + TEXT(" creation cleanup package")),
        FindPackage(nullptr, *FailedPackageName));

    const FString EditPackageName = Root + TEXT("/Edit");
    const FString EditObjectPath =
        FUnrealMCPAssetAuthoringKernel::ObjectPathForPackage(EditPackageName);
    UPackage* Package = CreatePackage(*EditPackageName);
    UObject* Asset = Fixture.CreateAsset(Package, TEXT("Edit"));
    if (!Test.TestNotNull(*(Fixture.FamilyId + TEXT(" edit fixture creates")), Asset))
    {
        return false;
    }
    Fixture.WriteValue(Asset, TEXT("Before"));
    Package->SetDirtyFlag(false);
    const FString UnrelatedBefore = Fixture.ReadUnrelatedValue(Asset);
    int32 MutationCount = 0;
    int32 PersistCount = 0;
    FUnrealMCPAssetEditHooks Hooks;
    Hooks.ReadBack = [&Fixture](UObject* Target, FString& Snapshot, FUnrealMCPError&)
    {
        Snapshot = SnapshotFor(Fixture.ReadValue(Target));
        return true;
    };
    Hooks.Mutate = [&Fixture, &MutationCount](UObject* Target, FUnrealMCPError&)
    {
        ++MutationCount;
        Fixture.WriteValue(Target, TEXT("Accepted"));
        return true;
    };
    Hooks.Persist = [&PersistCount](UObject*, FUnrealMCPError&)
    {
        ++PersistCount;
        return true;
    };
    FUnrealMCPAssetEditRequest Request{
        FString(), EditObjectPath, SnapshotFor(TEXT("stale")),
        TEXT("Asset-family conformance"), Asset, true, true};
    FUnrealMCPAssetEditResult EditResult;
    Error = {};
    Test.TestFalse(*(Fixture.FamilyId + TEXT(" stale edit rejects")),
        FUnrealMCPAssetAuthoringKernel::ExecuteEdit(Request, Hooks, EditResult, Error));
    Test.TestEqual(*(Fixture.FamilyId + TEXT(" stale edit error")),
        Error.Code, FString(TEXT("stale_precondition")));
    Test.TestEqual(*(Fixture.FamilyId + TEXT(" stale edit avoids semantics")),
        MutationCount, 0);

    Request.ExpectedSnapshot = SnapshotFor(TEXT("Before"));
    Hooks.Mutate = [&Fixture](UObject* Target, FUnrealMCPError&)
    {
        Fixture.WriteValue(Target, TEXT("Rejected"));
        return true;
    };
    Hooks.ReadBack = [&Fixture](UObject* Target, FString& Snapshot, FUnrealMCPError&)
    {
        const FString Value = Fixture.ReadValue(Target);
        if (Value == TEXT("Rejected")) return false;
        Snapshot = SnapshotFor(Value);
        return true;
    };
    Error = {};
    Test.TestFalse(*(Fixture.FamilyId + TEXT(" failed postcondition rejects")),
        FUnrealMCPAssetAuthoringKernel::ExecuteEdit(Request, Hooks, EditResult, Error));
    Test.TestEqual(*(Fixture.FamilyId + TEXT(" transaction recovery restores value")),
        Fixture.ReadValue(Asset), FString(TEXT("Before")));

    Hooks.ReadBack = [&Fixture](UObject* Target, FString& Snapshot, FUnrealMCPError&)
    {
        Snapshot = SnapshotFor(Fixture.ReadValue(Target));
        return true;
    };
    Hooks.Mutate = [&Fixture](UObject* Target, FUnrealMCPError&)
    {
        Fixture.WriteValue(Target, TEXT("Accepted"));
        return true;
    };
    Error = {};
    Test.TestTrue(*(Fixture.FamilyId + TEXT(" admitted edit commits")),
        FUnrealMCPAssetAuthoringKernel::ExecuteEdit(Request, Hooks, EditResult, Error));
    Test.TestEqual(*(Fixture.FamilyId + TEXT(" persistence and recovery execute")), PersistCount, 3);
    Test.TestEqual(*(Fixture.FamilyId + TEXT(" read-back snapshot")),
        EditResult.SnapshotId, SnapshotFor(TEXT("Accepted")));
    Test.TestEqual(*(Fixture.FamilyId + TEXT(" unrelated content preserved")),
        Fixture.ReadUnrelatedValue(Asset), UnrelatedBefore);
    Test.TestTrue(*(Fixture.FamilyId + TEXT(" edit is undoable")),
        GEditor != nullptr && GEditor->UndoTransaction());
    Test.TestEqual(*(Fixture.FamilyId + TEXT(" Undo restores value")),
        Fixture.ReadValue(Asset), FString(TEXT("Before")));
    Test.TestTrue(*(Fixture.FamilyId + TEXT(" edit is redoable")),
        GEditor != nullptr && GEditor->RedoTransaction());
    Test.TestEqual(*(Fixture.FamilyId + TEXT(" Redo restores value")),
        Fixture.ReadValue(Asset), FString(TEXT("Accepted")));
    if (GEditor != nullptr) GEditor->UndoTransaction();
    Fixture.Retire(Package, Asset);
    return true;
}

inline FString EncodeDocument(const FUnrealMCPAssetFamilyDocumentBuilder& Document)
{
    FString Encoded;
    for (const FUnrealMCPAssetFamilyValueRecord& Record : Document.GetRecords())
    {
        Encoded += Record.Path + TEXT("|") + Record.Type + TEXT("\n");
    }
    return Encoded;
}

inline bool ExerciseInspection(
    FAutomationTestBase& Test,
    const FUnrealMCPAssetFamilyDescriptor& Descriptor)
{
    FUnrealMCPAssetFamilyInspectionContext Context;
    Context.Asset = GetTransientPackage();
    Context.Identity.ObjectPath = TEXT("/Engine/Transient.Conformance");
    Context.Identity.SnapshotId = FString::ChrN(40, TEXT('0'));
    Context.Selector.Segments = {TEXT("items")};
    Context.PageIndex = 1;
    Context.PageSize = 2;
    Context.bHasPaging = true;

    auto Invoke = [&Descriptor, &Context](
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPAssetFamilySelectorRouter& Selectors,
        FString& OutSnapshot,
        FUnrealMCPError& OutError)
    {
        FUnrealMCPAssetFamilySnapshotBuilder Snapshot(Descriptor.Bounds);
        if (!Descriptor.InspectionAdapter->Inspect(
                Context, Document, Selectors, Snapshot, OutError)
            || !Selectors.Freeze(OutError))
        {
            return false;
        }
        OutSnapshot = Snapshot.BuildSnapshotId();
        return true;
    };

    FUnrealMCPAssetFamilyDocumentBuilder FirstDocument(Descriptor.Bounds);
    FUnrealMCPAssetFamilySelectorRouter FirstSelectors(Descriptor.Bounds);
    FUnrealMCPAssetFamilyDocumentBuilder SecondDocument(Descriptor.Bounds);
    FUnrealMCPAssetFamilySelectorRouter SecondSelectors(Descriptor.Bounds);
    FString FirstSnapshot;
    FString SecondSnapshot;
    FUnrealMCPError Error;
    if (!Test.TestTrue(
            *FString::Printf(TEXT("%s inspection adapter executes"), *Descriptor.FamilyId),
            Invoke(FirstDocument, FirstSelectors, FirstSnapshot, Error)))
    {
        Test.AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    if (!Test.TestTrue(
            *FString::Printf(TEXT("%s inspection adapter replays"), *Descriptor.FamilyId),
            Invoke(SecondDocument, SecondSelectors, SecondSnapshot, Error)))
    {
        return false;
    }
    Test.TestEqual(
        *FString::Printf(TEXT("%s deterministic document encoding"), *Descriptor.FamilyId),
        EncodeDocument(FirstDocument), EncodeDocument(SecondDocument));
    Test.TestEqual(
        *FString::Printf(TEXT("%s deterministic snapshot"), *Descriptor.FamilyId),
        FirstSnapshot, SecondSnapshot);
    Test.TestEqual(
        *FString::Printf(TEXT("%s snapshot length"), *Descriptor.FamilyId),
        FirstSnapshot.Len(), 40);
    const FUnrealMCPAssetFamilySelectorRoute* Route = FirstSelectors.Resolve(Context.Selector, Error);
    Test.TestTrue(
        *FString::Printf(TEXT("%s selector routes"), *Descriptor.FamilyId),
        Route != nullptr && Route->bPageable);
    return true;
}

inline bool ExerciseCreation(
    FAutomationTestBase& Test,
    const FUnrealMCPAssetFamilyDescriptor& Descriptor)
{
    FUnrealMCPAssetFamilyCreationContext Context;
    Context.Outer = GetTransientPackage();
    Context.AssetClass = Descriptor.NativeClass;
    Context.AssetName = TEXT("Conformance");
    Context.CanonicalObjectPath = TEXT("/Engine/Transient.Conformance");
    FUnrealMCPAssetFamilyDocumentBuilder ReadBack(Descriptor.Bounds);
    FUnrealMCPAssetFamilySnapshotBuilder Snapshot(Descriptor.Bounds);
    UObject* Asset = nullptr;
    FUnrealMCPError Error;
    const bool bCreated = Descriptor.CreationAdapter->Create(
        Context, Asset, ReadBack, Snapshot, Error);
    Test.TestTrue(
        *FString::Printf(TEXT("%s creation adapter executes"), *Descriptor.FamilyId),
        bCreated);
    Test.TestEqual(
        *FString::Printf(TEXT("%s creation returns the exact fixture"), *Descriptor.FamilyId),
        Asset, Context.Outer);
    Test.TestEqual(
        *FString::Printf(TEXT("%s creation snapshot length"), *Descriptor.FamilyId),
        Snapshot.BuildSnapshotId().Len(), 40);
    return bCreated;
}

inline bool ExerciseEditing(
    FAutomationTestBase& Test,
    const FUnrealMCPAssetFamilyDescriptor& Descriptor)
{
    FUnrealMCPAssetFamilyEditContext Context;
    Context.Asset = GetTransientPackage();
    Context.Identity.ObjectPath = TEXT("/Engine/Transient.Conformance");
    Context.Identity.SnapshotId = FString::ChrN(40, TEXT('0'));
    Context.Operation = TEXT("set_value");
    FUnrealMCPAssetFamilyDocumentBuilder ReadBack(Descriptor.Bounds);
    FUnrealMCPAssetFamilySnapshotBuilder Snapshot(Descriptor.Bounds);
    FUnrealMCPError Error;
    const bool bEdited = Descriptor.EditingAdapter->Edit(Context, ReadBack, Snapshot, Error);
    Test.TestTrue(
        *FString::Printf(TEXT("%s editing adapter executes"), *Descriptor.FamilyId),
        bEdited);
    Test.TestEqual(
        *FString::Printf(TEXT("%s editing snapshot length"), *Descriptor.FamilyId),
        Snapshot.BuildSnapshotId().Len(), 40);
    return bEdited;
}

inline bool ExerciseBounds(
    FAutomationTestBase& Test,
    const FUnrealMCPAssetFamilyDescriptor& Descriptor)
{
    FUnrealMCPError Error;
    FUnrealMCPAssetFamilyDocumentBuilder Document(Descriptor.Bounds);
    for (int32 Index = 0; Index < Descriptor.Bounds.MaxDocumentRecords; ++Index)
    {
        FUnrealMCPAssetFamilyValueRecord Record;
        Record.Path = FString::Printf(TEXT("limit/%d"), Index);
        Record.Type = TEXT("string");
        Record.Value = MakeShared<FUnrealMCPValueString>(TEXT("bounded"));
        if (!Document.Add(MoveTemp(Record), Error)) return false;
    }
    FUnrealMCPAssetFamilyValueRecord Overflow;
    Overflow.Path = TEXT("limit/overflow");
    Overflow.Type = TEXT("string");
    Overflow.Value = MakeShared<FUnrealMCPValueString>(TEXT("overflow"));
    Test.TestFalse(*(Descriptor.FamilyId + TEXT(" document limit rejects")),
        Document.Add(MoveTemp(Overflow), Error));
    Test.TestEqual(*(Descriptor.FamilyId + TEXT(" document limit error")),
        Error.Code, FString(TEXT("data_limit_exceeded")));

    FUnrealMCPAssetFamilySelectorRouter Selectors(Descriptor.Bounds);
    for (int32 Index = 0; Index < Descriptor.Bounds.MaxSelectorRoutes; ++Index)
    {
        const FString Identity = FString::Printf(TEXT("route_%d"), Index);
        if (!Selectors.Register({Identity, {Identity}, true, false}, Error)) return false;
    }
    Test.TestFalse(*(Descriptor.FamilyId + TEXT(" selector limit rejects")),
        Selectors.Register({TEXT("route_overflow"), {TEXT("overflow")}, true, false}, Error));
    Test.TestEqual(*(Descriptor.FamilyId + TEXT(" selector limit error")),
        Error.Code, FString(TEXT("data_limit_exceeded")));

    FUnrealMCPAssetFamilySnapshotBuilder Snapshot(Descriptor.Bounds);
    for (int32 Index = 0; Index < Descriptor.Bounds.MaxSnapshotContributions; ++Index)
    {
        if (!Snapshot.Add(
                FString::Printf(TEXT("snapshot_%d"), Index), TEXT("bounded"), Error))
        {
            return false;
        }
    }
    Test.TestFalse(*(Descriptor.FamilyId + TEXT(" snapshot limit rejects")),
        Snapshot.Add(TEXT("snapshot_overflow"), TEXT("overflow"), Error));
    Test.TestEqual(*(Descriptor.FamilyId + TEXT(" snapshot limit error")),
        Error.Code, FString(TEXT("data_limit_exceeded")));
    return true;
}

inline bool RunFixture(FAutomationTestBase& Test, const FFixture& Fixture)
{
    const FUnrealMCPAssetFamilyModuleResolver Resolver = [](FName Module)
    {
        return Module != TEXT("UnrealMCPConformanceMissing");
    };
    FUnrealMCPAssetFamilyRegistry First(Resolver);
    FUnrealMCPAssetFamilyRegistry Restarted(Resolver);
    FUnrealMCPError Error;
    if (!Test.TestTrue(*(Fixture.Label + TEXT(" registers")), First.Register(Fixture.MakeDescriptor(), Error))
        || !Test.TestTrue(*(Fixture.Label + TEXT(" freezes")), First.Freeze(Error))
        || !Test.TestTrue(*(Fixture.Label + TEXT(" restart registers")), Restarted.Register(Fixture.MakeDescriptor(), Error))
        || !Test.TestTrue(*(Fixture.Label + TEXT(" restart freezes")), Restarted.Freeze(Error)))
    {
        Test.AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    Test.TestEqual(*(Fixture.Label + TEXT(" restart fingerprint")),
        First.GetFingerprint(), Restarted.GetFingerprint());
    Test.TestEqual(*(Fixture.Label + TEXT(" family identity")),
        First.GetDescriptors()[0].FamilyId, Fixture.FamilyId);

    const TArray<TPair<EUnrealMCPAssetFamilyCapability, bool>> Capabilities = {
        {EUnrealMCPAssetFamilyCapability::Inspection, Fixture.Capabilities.bInspection},
        {EUnrealMCPAssetFamilyCapability::Creation, Fixture.Capabilities.bCreation},
        {EUnrealMCPAssetFamilyCapability::Editing, Fixture.Capabilities.bEditing},
    };
    FUnrealMCPAssetFamilySelection Selection;
    for (const TPair<EUnrealMCPAssetFamilyCapability, bool>& Capability : Capabilities)
    {
        Error = {};
        const bool bSelected = First.Select(
            Fixture.TargetClass, Capability.Key, Selection, Error);
        const bool bExpected = Fixture.bDependencyAvailable && Capability.Value;
        Test.TestEqual(*(Fixture.Label + TEXT(" capability selection")), bSelected, bExpected);
        if (!bExpected)
        {
            Test.TestEqual(*(Fixture.Label + TEXT(" unavailable-state error")), Error.Code,
                Fixture.bDependencyAvailable
                    ? FString(TEXT("unsupported_operation"))
                    : FString(TEXT("dependency_unavailable")));
        }
    }

    if (!Fixture.bDependencyAvailable || !Fixture.bExerciseAdapters)
    {
        return true;
    }
    const FUnrealMCPAssetFamilyDescriptor& Descriptor = First.GetDescriptors()[0];
    if (!ExerciseBounds(Test, Descriptor)) return false;
    if (Fixture.Capabilities.bInspection && !ExerciseInspection(Test, Descriptor)) return false;
    if (Fixture.Capabilities.bCreation && !ExerciseCreation(Test, Descriptor)) return false;
    if (Fixture.Capabilities.bEditing && !ExerciseEditing(Test, Descriptor)) return false;
    return true;
}

inline bool VerifyRegisteredFamily(
    FAutomationTestBase& Test,
    const FUnrealMCPAssetFamilyRegistry& Registry,
    const FString& FamilyId,
    UClass* TargetClass,
    FUnrealMCPAssetFamilyCapabilities Capabilities)
{
    const TArray<TPair<EUnrealMCPAssetFamilyCapability, bool>> Expected = {
        {EUnrealMCPAssetFamilyCapability::Inspection, Capabilities.bInspection},
        {EUnrealMCPAssetFamilyCapability::Creation, Capabilities.bCreation},
        {EUnrealMCPAssetFamilyCapability::Editing, Capabilities.bEditing},
    };
    FUnrealMCPAssetFamilySelection Selection;
    FUnrealMCPError Error;
    for (const TPair<EUnrealMCPAssetFamilyCapability, bool>& Capability : Expected)
    {
        const bool bSelected = Registry.Select(TargetClass, Capability.Key, Selection, Error);
        Test.TestEqual(*(FamilyId + TEXT(" built-in capability")), bSelected, Capability.Value);
        if (bSelected)
        {
            Test.TestEqual(*(FamilyId + TEXT(" built-in identity")),
                Selection.Descriptor->FamilyId, FamilyId);
        }
    }
    return true;
}
}

#endif
