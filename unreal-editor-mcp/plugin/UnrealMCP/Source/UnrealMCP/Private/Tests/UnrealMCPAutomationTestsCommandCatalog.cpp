#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "UnrealMCPCommandCatalog.h"

namespace
{
FUnrealMCPCommandDescriptor Descriptor(const TCHAR* Identity)
{
    FUnrealMCPCommandDescriptor Result;
    Result.Identity = Identity;
    Result.Handler = [](const TSharedPtr<FUnrealMCPRecord>&, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError&)
    {
        OutResult = MakeShared<FUnrealMCPRecord>();
        return true;
    };
    return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPCommandCatalogCompositionTest,
    "UnrealMCP.CommandCatalog.FixedCompositionAndRejection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPCommandCatalogCompositionTest::RunTest(const FString& Parameters)
{
    FUnrealMCPCommandCatalogBuilder Catalog;
    FString Error;

    FUnrealMCPCommandDescriptor Read = Descriptor(TEXT("read"));
    Read.Features.Add({TEXT("read_feature"), []() { return true; }});
    Read.Limits.Add({TEXT("read_records"), 4.0});
    TestTrue(TEXT("fixed read command registers"),
        Catalog.Register(MoveTemp(Read), EUnrealMCPCommandSource::FixedNative, false, Error));

    FUnrealMCPCommandDescriptor Write = Descriptor(TEXT("write"));
    Write.Access = EUnrealMCPCommandAccess::Writable;
    Write.RetainedOperation = EUnrealMCPRetainedOperationPolicy::Retained;
    TestTrue(TEXT("fixed retained mutation registers"),
        Catalog.Register(MoveTemp(Write), EUnrealMCPCommandSource::FixedNative, false, Error));

    const FUnrealMCPCommandDescriptor* WriteResult = Catalog.Find(TEXT("write"));
    TestTrue(TEXT("descriptor retains access and ledger policy"),
        WriteResult != nullptr
        && WriteResult->Access == EUnrealMCPCommandAccess::Writable
        && WriteResult->RetainedOperation == EUnrealMCPRetainedOperationPolicy::Retained);
    TestEqual(TEXT("command order is deterministic"),
        Catalog.BuildCommandNames()[0]->AsString(),
        FString(TEXT("read")));
    TestTrue(TEXT("capabilities and limits compose from descriptors"),
        Catalog.BuildFeatures()->GetBoolField(TEXT("read_feature"))
        && Catalog.BuildLimits()->GetNumberField(TEXT("read_records")) == 4.0);

    TestFalse(TEXT("duplicate commands reject"),
        Catalog.Register(Descriptor(TEXT("read")), EUnrealMCPCommandSource::FixedNative, false, Error));

    FUnrealMCPCommandCatalogBuilder RuntimeCatalog;
    TestFalse(TEXT("runtime commands reject"),
        RuntimeCatalog.Register(Descriptor(TEXT("runtime")), EUnrealMCPCommandSource::Runtime, false, Error));
    TestFalse(TEXT("native model schemas reject"),
        RuntimeCatalog.Register(Descriptor(TEXT("schema")), EUnrealMCPCommandSource::FixedNative, true, Error));

    FUnrealMCPCommandCatalogBuilder ConflictCatalog;
    FUnrealMCPCommandDescriptor First = Descriptor(TEXT("first"));
    First.Features.Add({TEXT("same"), []() { return true; }});
    TestTrue(TEXT("first capability registers"),
        ConflictCatalog.Register(MoveTemp(First), EUnrealMCPCommandSource::FixedNative, false, Error));
    FUnrealMCPCommandDescriptor Second = Descriptor(TEXT("second"));
    Second.Features.Add({TEXT("same"), []() { return false; }});
    TestFalse(TEXT("conflicting capabilities reject"),
        ConflictCatalog.Register(MoveTemp(Second), EUnrealMCPCommandSource::FixedNative, false, Error));

    TestTrue(TEXT("catalog freezes"), Catalog.Freeze(Error));
    TestNull(TEXT("frozen descriptors cannot be mutated"), Catalog.FindMutable(TEXT("read")));
    TestFalse(TEXT("late registration rejects"),
        Catalog.Register(Descriptor(TEXT("late")), EUnrealMCPCommandSource::FixedNative, false, Error));
    return true;
}

#endif
