#include "UnrealMCPAutomationTestSupport.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UObject/StrongObjectPtr.h"
#include "IUnrealMCPModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPExtensionRegistry.h"

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
    virtual bool ValidateArguments(const FString&, const TSharedPtr<FJsonObject>&,
        FUnrealMCPExtensionError&) const override { return true; }
    virtual bool Inspect(const UObject&, const FString&, const TSharedPtr<FJsonObject>&,
        TSharedPtr<FJsonObject>& OutResult, FUnrealMCPExtensionError&) override
    {
        OutResult = MakeShared<FJsonObject>();
        const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("section"), TEXT("synthetic_family"));
        OutResult->SetArrayField(TEXT("records"), {
            MakeShared<FJsonValueObject>(Record)});
        const TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
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
    virtual bool ApplyMutation(UObject&, const FString&, const TSharedPtr<FJsonObject>&,
        TSharedPtr<FJsonObject>&, FUnrealMCPExtensionError&) override { return true; }
    virtual bool ReadBack(const UObject&, const FString&, const TSharedPtr<FJsonObject>&,
        TSharedPtr<FJsonObject>& OutResult, FUnrealMCPExtensionError&) const override
    {
        OutResult = MakeShared<FJsonObject>();
        return true;
    }
};

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
    Registration.CompanionApiVersion = 1;
    Registration.ExtensionSchemaRevision = 1;
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
             "\"extension_schema_revision\":1,\"operation\":\"%s\",\"asset_path\":\"%s\"%s}}"),
        *Command, *Operation, *AssetPath,
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
        if (State->Step >= 8) return true;
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
        case 1: Json = RequestJson(TEXT("blueprint_inspect"), TEXT("inspect_test_asset"), State->AssetPath); break;
        case 2: Json = RequestJson(TEXT("blueprint_default_edit"), TEXT("set_test_asset_value"), State->AssetPath, State->AssetSnapshot, 11); break;
        case 3: Json = RequestJson(TEXT("blueprint_inspect"), TEXT("inspect_test_component"), State->BlueprintPath); break;
        case 4: Json = RequestJson(TEXT("blueprint_component_edit"), TEXT("set_test_component_value"), State->BlueprintPath, State->ComponentSnapshot, 22); break;
        case 5: Json = RequestJson(TEXT("blueprint_inspect"), TEXT("inspect_test_contribution"), State->BlueprintPath); break;
        case 6: Json = RequestJson(TEXT("blueprint_default_edit"), TEXT("set_test_contribution_value"), State->BlueprintPath, State->ExistingSnapshot, 33); break;
        default: Json = RequestJson(TEXT("blueprint_default_edit"), TEXT("set_test_asset_value"), State->AssetPath, State->AssetSnapshot, 44); break;
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
        if (State->Step == 7)
        {
            Test.TestEqual(TEXT("stale companion mutation rejects"), State->ResponseCode,
                static_cast<int32>(EHttpServerResponseCodes::BadRequest));
            Test.TestTrue(TEXT("stale companion response has an error"),
                State->Envelope->TryGetObjectField(TEXT("error"), Object) && Object != nullptr);
            if (Object != nullptr)
            {
                Test.TestEqual(TEXT("stale companion error is stable"),
                    (*Object)->GetStringField(TEXT("code")), FString(TEXT("stale_precondition")));
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
                static_cast<int32>((*Object)->GetNumberField(TEXT("companion_api_version"))), 1);
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
        else if (State->Step == 1) State->AssetSnapshot = (*Object)->GetStringField(TEXT("snapshot"));
        else if (State->Step == 3) State->ComponentSnapshot = (*Object)->GetStringField(TEXT("snapshot"));
        else if (State->Step == 5) State->ExistingSnapshot = (*Object)->GetStringField(TEXT("snapshot"));
        else if (State->Step == 2 || State->Step == 4 || State->Step == 6)
        {
            const int32 Expected = State->Step == 2 ? 11 : (State->Step == 4 ? 22 : 33);
            Test.TestEqual(TEXT("companion mutation read-back value"),
                static_cast<int32>((*Object)->GetNumberField(TEXT("value"))), Expected);
            Test.TestEqual(TEXT("companion mutation commits through the operation ledger"),
                (*Object)->GetStringField(TEXT("operation_state")), FString(TEXT("committed")));
        }
    }

    FAutomationTestBase& Test;
    TSharedRef<FCompanionBridgeState> State;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPCompanionBridgeTest,
    "UnrealMCP.Companions.AuthenticatedBridgeRoundTrip",
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
        FUnrealMCPExtensionRegistry Registry;
        FUnrealMCPCompanionRegistration Registration = SyntheticRegistration(
            TEXT("synthetic_api"), TEXT("inspect_synthetic_api"));
        Registry.AddDescriptorForTesting(Registration);
        Registration.CompanionApiVersion = 2;
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
        Registration.ExtensionSchemaRevision = 2;
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
        const TSharedPtr<FJsonObject> Capabilities = Registry.BuildCapabilities();
        const TArray<TSharedPtr<FJsonValue>>& Companions =
            Capabilities->GetArrayField(TEXT("companions"));
        TSharedPtr<FJsonObject> FirstCapability;
        for (const TSharedPtr<FJsonValue>& Value : Companions)
        {
            const TSharedPtr<FJsonObject> Candidate = Value->AsObject();
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
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("mode"), TEXT("inspect"));
    Arguments->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    Arguments->SetArrayField(TEXT("sections"), {
        MakeShared<FJsonValueString>(TEXT("summary"))});
    TSharedPtr<FJsonObject> Result;
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
