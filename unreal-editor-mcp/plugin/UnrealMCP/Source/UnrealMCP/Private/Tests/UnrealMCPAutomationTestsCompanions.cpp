#include "UnrealMCPAutomationTestSupport.h"
#include "UnrealMCPHostAutomationTestSupport.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UObject/StrongObjectPtr.h"
#include "IUnrealMCPModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPExtensionRegistry.h"
#include "UnrealMCPVersion.h"

namespace
{
using namespace UnrealMCP::Tests;

class FSyntheticCompanionHandler final : public IUnrealMCPExtensionHandler
{
public:
    virtual bool IsReady(FString& OutUnavailableReason) const override
    {
        OutUnavailableReason.Reset();
        return true;
    }
    virtual bool SupportsTarget(const UObject& Target) const override { return true; }
    virtual bool ValidateArguments(const FString&, const TSharedPtr<FUnrealMCPRecord>&,
        FUnrealMCPExtensionError&) const override { return true; }
    virtual bool Inspect(const UObject&, const FString&, const TSharedPtr<FUnrealMCPRecord>&,
        TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPExtensionError&) override
    {
        OutResult = MakeShared<FUnrealMCPRecord>();
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("section"), TEXT("synthetic_family"));
        OutResult->SetArrayField(TEXT("records"), {
            MakeShared<FUnrealMCPValueObject>(Record)});
        const TSharedRef<FUnrealMCPRecord> Capabilities = MakeShared<FUnrealMCPRecord>();
        Capabilities->SetBoolField(TEXT("inspection"), true);
        Capabilities->SetBoolField(TEXT("mutation"), false);
        OutResult->SetObjectField(TEXT("family_capabilities"), Capabilities);
        return true;
    }
    virtual bool AppendFingerprint(const UObject&, const FString&, FString& OutFingerprint,
        FUnrealMCPExtensionError&) const override
    {
        OutFingerprint = TEXT("synthetic");
        return true;
    }
    virtual bool ApplyMutation(UObject&, const FString&, const TSharedPtr<FUnrealMCPRecord>&,
        TSharedPtr<FUnrealMCPRecord>&, FUnrealMCPExtensionError&) override { return true; }
    virtual bool ReadBack(const UObject&, const FString&, const TSharedPtr<FUnrealMCPRecord>&,
        TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPExtensionError&) const override
    {
        OutResult = MakeShared<FUnrealMCPRecord>();
        return true;
    }
};

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
        return Selectors.Register({TEXT("summary"), {TEXT("summary")}, false, false}, OutError)
            && Document.Add({TEXT("summary.kind"), TEXT("string"),
                MakeShared<FUnrealMCPValueString>(TEXT("synthetic"))}, OutError)
            && Snapshot.Add(TEXT("summary"), TEXT("synthetic"), OutError);
    }
};

class FSyntheticCreationAdapter final : public IUnrealMCPAssetFamilyCreationAdapter
{
public:
    bool Create(
        const FUnrealMCPAssetFamilyCreationContext&,
        UObject*& OutAsset,
        FUnrealMCPAssetFamilyDocumentBuilder&,
        FUnrealMCPAssetFamilySnapshotBuilder&,
        FUnrealMCPError&) override
    {
        OutAsset = nullptr;
        return true;
    }
};

class FSyntheticEditingAdapter final : public IUnrealMCPAssetFamilyEditingAdapter
{
public:
    bool Edit(
        const FUnrealMCPAssetFamilyEditContext&,
        FUnrealMCPAssetFamilyDocumentBuilder&,
        FUnrealMCPAssetFamilySnapshotBuilder&,
        FUnrealMCPError&) override
    {
        return true;
    }
};

class FSyntheticWrongOwner final : public IModuleInterface
{
};

FUnrealMCPCompanionAssetFamily SyntheticAssetFamily()
{
    FUnrealMCPCompanionAssetFamily Family;
    Family.FamilyId = TEXT("synthetic_v2");
    Family.NativeClassPath = TEXT("/Script/CoreUObject.Object");
    Family.ClassPolicy = EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived;
    Family.Priority = 25;
    Family.Limits = {{TEXT("records"), 4}};
    Family.Capabilities = {true, true, true};
    Family.SelectorRoutes = {{TEXT("summary"), {TEXT("summary")}, false, false}};
    Family.StableNestedIdentityKinds = {TEXT("entry")};
    Family.CreationPersistence = EUnrealMCPExtensionPersistence::PackageSave;
    Family.EditingPersistence = EUnrealMCPExtensionPersistence::PackageSave;
    Family.InspectionAdapter = MakeShared<FSyntheticInspectionAdapter>();
    Family.CreationAdapter = MakeShared<FSyntheticCreationAdapter>();
    Family.EditingAdapter = MakeShared<FSyntheticEditingAdapter>();
    Family.SnapshotBuilder = [](UObject* Asset)
    {
        return Asset != nullptr ? Asset->GetPathName() : FString();
    };
    return Family;
}

FUnrealMCPCompanionRegistration SyntheticRegistration(
    const FString& ExtensionId,
    const FString& Operation,
    const FString& TargetFamily = TEXT("synthetic_target"))
{
    FUnrealMCPExtensionContribution Contribution;
    Contribution.ContributionId = ExtensionId + TEXT("_read");
    Contribution.Category = EUnrealMCPExtensionCategory::AssetFamily;
    Contribution.Access = EUnrealMCPExtensionAccess::Read;
    Contribution.ToolFamily = TEXT("blueprint_inspect");
    Contribution.Operation = Operation;
    Contribution.TargetFamily = TargetFamily;
    Contribution.TargetClassPath = TEXT("/Script/CoreUObject.Object");
    Contribution.bAllowDerivedTargetClasses = true;
    Contribution.Handler = MakeShared<FSyntheticCompanionHandler>();
    FUnrealMCPCompanionRegistration Registration;
    Registration.PluginName = TEXT("SyntheticPlugin") + ExtensionId;
    Registration.ExtensionId = ExtensionId;
    Registration.OwningModule = TEXT("UnrealMCP");
    Registration.SemanticVersion = TEXT("7.4.2");
    Registration.CompanionApiVersion = UnrealMCP::CompanionApiVersion;
    Registration.ExtensionSchemaRevision = UnrealMCP::ExtensionSchemaRevision;
    Registration.Contributions = {Contribution};
    return Registration;
}

struct FCompanionBridgeState
{
    TSharedPtr<IHttpRouter> Router;
    FString Authorization;
    FString AssetPath;
    FString BlueprintPath;
    FString AssetSnapshot;
    FString ComponentSnapshot;
    FString ExistingSnapshot;
    int32 Step = 0;
    bool bWaiting = false;
    bool bResponseReady = false;
    int32 ResponseCode = 0;
    TSharedPtr<FJsonObject> Envelope;
    TStrongObjectPtr<UObject> Asset;
    TStrongObjectPtr<UBlueprint> Blueprint;
};

FString RequestJson(const FString& Command, const FString& Operation, const FString& AssetPath,
    const FString& Snapshot = FString(), int32 Value = 0)
{
    const bool bMutation = !Snapshot.IsEmpty();
    return FString::Printf(
        TEXT("{\"command\":\"%s\",\"arguments\":{\"extension_id\":\"unreal-mcp-test\","
             "\"extension_schema_revision\":%d,\"operation\":\"%s\",\"asset_path\":\"%s\"%s}}"),
        *Command, UnrealMCP::ExtensionSchemaRevision, *Operation, *AssetPath,
        bMutation
            ? *FString::Printf(TEXT(",\"operation_id\":\"%s\",\"expected_snapshot\":\"%s\",\"value\":%d"),
                *FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower(), *Snapshot, Value)
            : TEXT(""));
}

bool ParseEnvelope(const FHttpServerResponse& Response, TSharedPtr<FJsonObject>& OutEnvelope)
{
    FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Response.Body.GetData()), Response.Body.Num());
    return FJsonSerializer::Deserialize(
        TJsonReaderFactory<>::Create(FString(Converted.Length(), Converted.Get())), OutEnvelope)
        && OutEnvelope.IsValid();
}

class FCompanionBridgeLatentCommand final : public IAutomationLatentCommand
{
public:
    FCompanionBridgeLatentCommand(FAutomationTestBase& InTest, TSharedRef<FCompanionBridgeState> InState)
        : Test(InTest), State(MoveTemp(InState)) {}

    virtual bool Update() override
    {
        if (State->bWaiting)
        {
            if (!State->bResponseReady) return false;
            State->bWaiting = false;
            State->bResponseReady = false;
            CheckResponse();
            ++State->Step;
        }
        if (State->Step >= 2) return true;
        SendCurrent();
        return false;
    }

private:
    void SendCurrent()
    {
        FString Json;
        switch (State->Step)
        {
        case 0: Json = TEXT("{\"command\":\"capabilities\",\"arguments\":{}}"); break;
        default: Json = RequestJson(TEXT("blueprint_inspect"), TEXT("inspect_test_asset"), State->AssetPath); break;
        }
        State->Envelope.Reset();
        State->ResponseCode = 0;
        State->bWaiting = true;
        const bool bMatched = State->Router->Query(
            MakeJsonRequest(State->Authorization, Json),
            [State = State](TUniquePtr<FHttpServerResponse>&& Response)
            {
                if (Response.IsValid())
                {
                    State->ResponseCode = static_cast<int32>(Response->Code);
                    ParseEnvelope(*Response, State->Envelope);
                }
                State->bResponseReady = true;
            });
        Test.TestTrue(TEXT("companion request reaches the authenticated base route"), bMatched);
        if (!bMatched)
        {
            State->bResponseReady = true;
        }
    }

    void CheckResponse()
    {
        Test.TestTrue(TEXT("companion response is valid JSON"), State->Envelope.IsValid());
        if (!State->Envelope.IsValid()) return;
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (State->Step == 1)
        {
            Test.TestEqual(TEXT("removed companion inspection route rejects"), State->ResponseCode,
                static_cast<int32>(EHttpServerResponseCodes::BadRequest));
            Test.TestTrue(TEXT("removed companion inspection response has an error"),
                State->Envelope->TryGetObjectField(TEXT("error"), Object) && Object != nullptr);
            if (Object != nullptr)
            {
                Test.TestEqual(TEXT("removed tool error is stable"),
                    (*Object)->GetStringField(TEXT("code")), FString(TEXT("invalid_argument")));
            }
            return;
        }
        Test.TestEqual(TEXT("companion request succeeds"), State->ResponseCode,
            static_cast<int32>(EHttpServerResponseCodes::Ok));
        Test.TestTrue(TEXT("companion response has a result"),
            State->Envelope->TryGetObjectField(TEXT("result"), Object) && Object != nullptr);
        if (Object == nullptr) return;
        if (State->Step == 0)
        {
            Test.TestEqual(TEXT("native companion API version"),
                static_cast<int32>((*Object)->GetNumberField(TEXT("companion_api_version"))),
                UnrealMCP::CompanionApiVersion);
            const TArray<TSharedPtr<FJsonValue>>* Companions = nullptr;
            Test.TestTrue(TEXT("capabilities publish the test companion"),
                (*Object)->TryGetArrayField(TEXT("companions"), Companions)
                    && Companions != nullptr && !Companions->IsEmpty());
            if (Companions != nullptr)
            {
                TSharedPtr<FJsonObject> Companion;
                for (const TSharedPtr<FJsonValue>& Value : *Companions)
                {
                    const TSharedPtr<FJsonObject> Candidate = Value->AsObject();
                    if (Candidate.IsValid()
                        && Candidate->GetStringField(TEXT("extension_id")) == TEXT("unreal-mcp-test"))
                    {
                        Companion = Candidate;
                        break;
                    }
                }
                Test.TestTrue(TEXT("test companion record is present"), Companion.IsValid());
                if (!Companion.IsValid()) return;
                Test.TestEqual(TEXT("test companion identity"), Companion->GetStringField(TEXT("extension_id")),
                    FString(TEXT("unreal-mcp-test")));
                Test.TestTrue(TEXT("test companion is ready"), Companion->GetBoolField(TEXT("ready")));
                Test.TestEqual(TEXT("all fixture contributions are published"),
                    Companion->GetArrayField(TEXT("contributions")).Num(), 6);
            }
        }
    }

    FAutomationTestBase& Test;
    TSharedRef<FCompanionBridgeState> State;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPCompanionBridgeTest,
    "UnrealMCP.Companions.CapabilitiesAndRemovedInspectionRoute",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPCompanionBridgeTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const TSharedRef<FCompanionBridgeState> State = MakeShared<FCompanionBridgeState>();
    State->Router = FHttpServerModule::Get().GetHttpRouter(UnrealMCP::DefaultPort, true);
    FString Token;
    TestTrue(TEXT("live companion bridge router is available"), State->Router.IsValid());
    TestTrue(TEXT("live companion bridge token is readable"), LoadLiveToken(Token));
    if (!State->Router.IsValid() || Token.IsEmpty()) return false;
    State->Authorization = FString(TEXT("Bearer ")) + Token;

    UClass* AssetClass = LoadObject<UClass>(nullptr,
        TEXT("/Script/UnrealMCPTestCompanion.UnrealMCPTestAsset"));
    UClass* ComponentClass = LoadObject<UClass>(nullptr,
        TEXT("/Script/UnrealMCPTestCompanion.UnrealMCPTestComponent"));
    TestNotNull(TEXT("test asset class is loaded from the companion"), AssetClass);
    TestNotNull(TEXT("test component class is loaded from the companion"), ComponentClass);
    if (AssetClass == nullptr || ComponentClass == nullptr) return false;

    UObject* Asset = LoadObject<UObject>(nullptr,
        TEXT("/Game/UnrealMCPCompanion/DA_TestAsset.DA_TestAsset"));
    TestNotNull(TEXT("startup created the transient test asset"), Asset);
    if (Asset == nullptr) return false;
    State->Asset.Reset(Asset);
    State->AssetPath = Asset->GetPathName();

    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr,
        TEXT("/Game/UnrealMCPCompanion/BP_TestActor.BP_TestActor"));
    TestNotNull(TEXT("startup created the transient component Blueprint"), Blueprint);
    if (Blueprint == nullptr) return false;
    State->Blueprint.Reset(Blueprint);
    State->BlueprintPath = Blueprint->GetPathName();

    AddCommand(new FCompanionBridgeLatentCommand(*this, State));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPCompanionAdmissionTest,
    "UnrealMCP.Companions.AdmissionAndLifecycleFailures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPCompanionAdmissionTest::RunTest(const FString& Parameters)
{
    IUnrealMCPModule& Owner = IUnrealMCPModule::Get();

    {
        const TSharedRef<FUnrealMCPAssetFamilyRegistry> IntegratedFamilies =
            MakeShared<FUnrealMCPAssetFamilyRegistry>([](FName) { return true; });
        FUnrealMCPExtensionRegistry Registry(IntegratedFamilies);
        FUnrealMCPCompanionRegistration Registration = SyntheticRegistration(
            TEXT("synthetic_v2_family"), TEXT("inspect_synthetic_v2_family"));
        Registration.AssetFamilies = {SyntheticAssetFamily()};
        Registry.AddDescriptorForTesting(Registration);
        const FUnrealMCPRegistrationResult RegistrationResult =
            Registry.Register(Registration, Owner);
        TestTrue(TEXT("complete typed API-v2 family contract registers"),
            RegistrationResult.bAccepted);
        FUnrealMCPCompanionRegistration Collision = SyntheticRegistration(
            TEXT("synthetic_v2_family_collision"),
            TEXT("inspect_synthetic_v2_family_collision"),
            TEXT("synthetic_v2_family_collision_target"));
        Collision.AssetFamilies = {SyntheticAssetFamily()};
        Registry.AddDescriptorForTesting(Collision);
        TestEqual(TEXT("typed family collision fails closed"),
            Registry.Register(Collision, Owner).Reason,
            FString(TEXT("asset_family_collision")));
        FUnrealMCPError FamilyError;
        TestTrue(TEXT("admitted companion families freeze in the common registry"),
            IntegratedFamilies->Freeze(FamilyError));
        TArray<const FUnrealMCPAssetFamilyDescriptor*> Overlays;
        TestTrue(TEXT("common registry selects the admitted inspection overlay"),
            IntegratedFamilies->SelectInspectionOverlays(
                UObject::StaticClass(), Overlays, FamilyError));
        TestEqual(TEXT("exactly one inspection overlay is integrated"), Overlays.Num(), 1);
        if (Overlays.Num() == 1)
        {
            TestEqual(TEXT("integrated family identity is preserved"),
                Overlays[0]->FamilyId, FString(TEXT("synthetic_v2")));
        }
        Registry.Freeze();
        const TSharedPtr<FUnrealMCPRecord> Capabilities = Registry.BuildCapabilities();
        const TSharedPtr<FUnrealMCPRecord> Companion =
            Capabilities->GetArrayField(TEXT("companions"))[0]->AsObject();
        TestEqual(TEXT("typed family capability is published"),
            Companion->GetArrayField(TEXT("asset_families")).Num(), 1);
        const TSharedPtr<FUnrealMCPRecord> Family =
            Companion->GetArrayField(TEXT("asset_families"))[0]->AsObject();
        TestEqual(TEXT("typed family identity is stable"),
            Family->GetStringField(TEXT("family_id")), FString(TEXT("synthetic_v2")));
        const TSharedPtr<FUnrealMCPRecord> Operations = Family->GetObjectField(TEXT("operations"));
        TestTrue(TEXT("typed inspection seam is published"), Operations->GetBoolField(TEXT("inspect")));
        TestTrue(TEXT("target-free creation seam is published"), Operations->GetBoolField(TEXT("create")));
        TestTrue(TEXT("existing-target edit seam is published"), Operations->GetBoolField(TEXT("edit")));
        TestEqual(TEXT("stable nested identities are bounded and published"),
            Family->GetArrayField(TEXT("stable_nested_identity_kinds")).Num(), 1);
        Registry.BeginShutdown();
        FSyntheticWrongOwner WrongOwner;
        Registry.Unregister(RegistrationResult.Handle, WrongOwner);
        TestEqual(TEXT("wrong owner cannot unregister during shutdown"),
            Registry.AcceptedCountForTesting(), 1);
        Registry.Unregister(RegistrationResult.Handle, Owner);
        TestEqual(TEXT("owning module can unregister during shutdown"),
            Registry.AcceptedCountForTesting(), 0);
    }
    {
        FUnrealMCPExtensionRegistry Registry;
        FUnrealMCPCompanionRegistration Registration = SyntheticRegistration(
            TEXT("synthetic_invalid_family"), TEXT("inspect_synthetic_invalid_family"));
        FUnrealMCPCompanionAssetFamily Family = SyntheticAssetFamily();
        Family.CreationAdapter.Reset();
        Registration.AssetFamilies = {MoveTemp(Family)};
        Registry.AddDescriptorForTesting(Registration);
        TestEqual(TEXT("capability and adapter mismatch fails closed"),
            Registry.Register(Registration, Owner).Reason, FString(TEXT("invalid_asset_family")));
    }
    {
        FUnrealMCPExtensionRegistry Registry;
        FUnrealMCPCompanionRegistration Registration = SyntheticRegistration(
            TEXT("synthetic_invalid_policy"), TEXT("inspect_synthetic_invalid_policy"));
        FUnrealMCPCompanionAssetFamily Family = SyntheticAssetFamily();
        Family.ClassPolicy = static_cast<EUnrealMCPAssetFamilyClassPolicy>(255);
        Registration.AssetFamilies = {MoveTemp(Family)};
        Registry.AddDescriptorForTesting(Registration);
        TestEqual(TEXT("unknown class policy fails closed"),
            Registry.Register(Registration, Owner).Reason, FString(TEXT("invalid_asset_family")));
    }

    {
        FUnrealMCPExtensionRegistry Registry;
        FUnrealMCPCompanionRegistration Registration = SyntheticRegistration(
            TEXT("synthetic_api"), TEXT("inspect_synthetic_api"));
        Registry.AddDescriptorForTesting(Registration);
        Registration.CompanionApiVersion = 1;
        TestEqual(TEXT("compiled API mismatch fails closed"),
            Registry.Register(Registration, Owner).Reason, FString(TEXT("compiled_api_mismatch")));
        TestEqual(TEXT("rejected API mismatch has no partial admission"),
            Registry.AcceptedCountForTesting(), 0);
    }
    {
        FUnrealMCPExtensionRegistry Registry;
        FUnrealMCPCompanionRegistration Registration = SyntheticRegistration(
            TEXT("synthetic_schema"), TEXT("inspect_synthetic_schema"));
        Registry.AddDescriptorForTesting(Registration);
        Registration.ExtensionSchemaRevision = 1;
        TestEqual(TEXT("compiled schema mismatch fails closed"),
            Registry.Register(Registration, Owner).Reason,
            FString(TEXT("unsupported_schema_revision")));
    }
    {
        FUnrealMCPExtensionRegistry Registry;
        FUnrealMCPCompanionRegistration Registration = SyntheticRegistration(
            TEXT("synthetic_version"), TEXT("inspect_synthetic_version"));
        Registry.AddDescriptorForTesting(Registration);
        Registration.SemanticVersion = TEXT("7.4.3");
        TestEqual(TEXT("descriptor and compiled semantic versions must agree internally"),
            Registry.Register(Registration, Owner).Reason,
            FString(TEXT("descriptor_compiled_disagreement")));
    }
    {
        FUnrealMCPExtensionRegistry Registry;
        FUnrealMCPCompanionRegistration Registration = SyntheticRegistration(
            TEXT("synthetic_disabled"), TEXT("inspect_synthetic_disabled"));
        Registry.AddDescriptorForTesting(Registration, false, TEXT("disabled"));
        TestEqual(TEXT("disabled descriptors expose no registration"),
            Registry.Register(Registration, Owner).Reason, FString(TEXT("descriptor_unavailable")));
        TestEqual(TEXT("disabled descriptor has no partial admission"),
            Registry.AcceptedCountForTesting(), 0);
    }
    {
        FUnrealMCPExtensionRegistry Registry;
        FUnrealMCPCompanionRegistration First = SyntheticRegistration(
            TEXT("synthetic_first"), TEXT("inspect_synthetic_first"));
        FUnrealMCPCompanionRegistration Collision = SyntheticRegistration(
            TEXT("synthetic_collision"), TEXT("inspect_synthetic_collision"));
        Registry.AddDescriptorForTesting(First);
        Registry.AddDescriptorForTesting(Collision);
        TestTrue(TEXT("independent semantic version registration is accepted"),
            Registry.Register(First, Owner).bAccepted);
        TestEqual(TEXT("target-family collision fails closed"),
            Registry.Register(Collision, Owner).Reason, FString(TEXT("contribution_collision")));
        TestEqual(TEXT("collision preserves the first atomic registration only"),
            Registry.AcceptedCountForTesting(), 1);
        Registry.Freeze();
        FUnrealMCPCompanionRegistration Late = SyntheticRegistration(
            TEXT("synthetic_late"), TEXT("inspect_synthetic_late"), TEXT("late_target"));
        Registry.AddDescriptorForTesting(Late);
        TestEqual(TEXT("late registration is rejected after freeze"),
            Registry.Register(Late, Owner).Reason, FString(TEXT("registration_closed")));
        Registry.BeginShutdown();
        const TSharedPtr<FUnrealMCPRecord> Capabilities = Registry.BuildCapabilities();
        const TArray<TSharedPtr<FUnrealMCPValue>>& Companions =
            Capabilities->GetArrayField(TEXT("companions"));
        TSharedPtr<FUnrealMCPRecord> FirstCapability;
        for (const TSharedPtr<FUnrealMCPValue>& Value : Companions)
        {
            const TSharedPtr<FUnrealMCPRecord> Candidate = Value->AsObject();
            if (Candidate->GetStringField(TEXT("extension_id")) == TEXT("synthetic_first"))
            {
                FirstCapability = Candidate;
                break;
            }
        }
        TestTrue(TEXT("accepted companion remains in bounded shutdown capabilities"),
            FirstCapability.IsValid());
        if (!FirstCapability.IsValid()) return false;
        TestFalse(TEXT("shutdown immediately withdraws readiness"),
            FirstCapability->GetBoolField(TEXT("ready")));
        TestEqual(TEXT("shutdown readiness reason is stable"),
            FirstCapability->GetStringField(TEXT("unavailable_reason")),
            FString(TEXT("shutting_down")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPCompanionBlueprintFamilyInspectionTest,
    "UnrealMCP.Companions.BlueprintFamilyInspectionIntegration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPCompanionBlueprintFamilyInspectionTest::RunTest(const FString& Parameters)
{
    IUnrealMCPModule& Owner = IUnrealMCPModule::Get();
    FUnrealMCPExtensionRegistry Registry;
    FUnrealMCPCompanionRegistration Registration = SyntheticRegistration(
        TEXT("synthetic_blueprint_family"), TEXT("inspect_synthetic_blueprint_family"));
    Registration.Contributions[0].StableLimits.Add(TEXT("records"), 1);
    Registry.AddDescriptorForTesting(Registration);
    TestTrue(TEXT("synthetic Blueprint family registers"),
        Registry.Register(Registration, Owner).bAccepted);
    Registry.Freeze();

    UPackage* Package = CreatePackage(TEXT("/Engine/Transient/UnrealMCPSyntheticFamilyTest"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        UObject::StaticClass(), Package, TEXT("BP_SyntheticFamily"), BPTYPE_Normal,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    TestNotNull(TEXT("synthetic UObject Blueprint is created"), Blueprint);
    if (Blueprint == nullptr)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    const bool bDirtyBefore = Package->IsDirty();
    FUnrealMCPBlueprintInspector Inspector(Registry);
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(TEXT("mode"), TEXT("inspect"));
    Arguments->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Arguments->SetArrayField(TEXT("sections"), {
        MakeShared<FUnrealMCPValueString>(TEXT("summary"))});
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    TestTrue(TEXT("standard Blueprint inspector accepts the extension family"),
        Inspector.Execute(Arguments, Result, Error));
    if (!Result.IsValid())
    {
        return false;
    }
    TestEqual(TEXT("extension family identity is authoritative"),
        Result->GetStringField(TEXT("blueprint_family")), FString(TEXT("synthetic_target")));
    TestEqual(TEXT("extension record participates in the standard page"),
        Result->GetArrayField(TEXT("records")).Num(), 2);
    TestTrue(TEXT("extension family capabilities are nested by family"),
        Result->GetObjectField(TEXT("family_capabilities"))->HasField(TEXT("synthetic_target")));
    TestEqual(TEXT("inspection preserves package dirtiness"),
        Package->IsDirty(), bDirtyBefore);
    TestEqual(TEXT("published extension family matrix has one record"),
        Registry.BuildBlueprintFamilyCapabilities().Num(), 1);
    return true;
}

#endif
