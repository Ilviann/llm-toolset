#include "IUnrealMCPModule.h"
#include "UnrealMCPCompanionApi.h"
#include "UnrealMCPTestTypes.h"
#include "UnrealMCPTestCompanionVersion.h"

#include "Dom/JsonObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Modules/ModuleManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
enum class EFixtureTarget : uint8
{
    Asset,
    Component,
    ExistingBlueprint,
};

class FFixtureHandler final : public IUnrealMCPExtensionHandler
{
public:
    explicit FFixtureHandler(EFixtureTarget InTarget) : Target(InTarget) {}

    virtual bool IsReady(FString& OutUnavailableReason) const override
    {
        OutUnavailableReason.Reset();
        return true;
    }

    virtual bool SupportsTarget(const UObject& Object) const override
    {
        if (Target == EFixtureTarget::Asset) return Object.IsA<UUnrealMCPTestAsset>();
        const UBlueprint* Blueprint = Cast<UBlueprint>(&Object);
        return Blueprint != nullptr && (Target != EFixtureTarget::Component || FindComponent(*Blueprint) != nullptr);
    }

    virtual bool ValidateArguments(
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Arguments,
        FUnrealMCPExtensionError& OutError) const override
    {
        if (!Arguments.IsValid())
        {
            OutError = {TEXT("invalid_argument"), TEXT("Fixture arguments are required")};
            return false;
        }
        if (Operation.StartsWith(TEXT("set_")))
        {
            double Value = 0.0;
            if (!Arguments->TryGetNumberField(TEXT("value"), Value)
                || !FMath::IsNearlyEqual(Value, FMath::RoundToDouble(Value))
                || Value < -1000000.0 || Value > 1000000.0)
            {
                OutError = {TEXT("invalid_argument"), TEXT("value must be a bounded integer")};
                return false;
            }
        }
        return true;
    }

    virtual bool Inspect(
        const UObject& Object,
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPExtensionError& OutError) override
    {
        return ReadValue(Object, OutResult, OutError);
    }

    virtual bool AppendFingerprint(
        const UObject& Object,
        const FString& Operation,
        FString& OutFingerprint,
        FUnrealMCPExtensionError& OutError) const override
    {
        int32 Value = 0;
        if (!GetValue(Object, Value))
        {
            OutError = {TEXT("invalid_asset"), TEXT("The fixture target is unavailable")};
            return false;
        }
        OutFingerprint = FString::Printf(TEXT("fixture|%d"), Value);
        return true;
    }

    virtual bool ApplyMutation(
        UObject& Object,
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutChange,
        FUnrealMCPExtensionError& OutError) override
    {
        double Number = 0.0;
        if (!Arguments.IsValid() || !Arguments->TryGetNumberField(TEXT("value"), Number))
        {
            OutError = {TEXT("invalid_argument"), TEXT("value is required")};
            return false;
        }
        const int32 Value = static_cast<int32>(Number);
        if (!SetValue(Object, Value))
        {
            OutError = {TEXT("invalid_asset"), TEXT("The fixture target cannot be mutated")};
            return false;
        }
        OutChange = MakeShared<FJsonObject>();
        OutChange->SetNumberField(TEXT("value"), Value);
        return true;
    }

    virtual bool ReadBack(
        const UObject& Object,
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPExtensionError& OutError) const override
    {
        return ReadValue(Object, OutResult, OutError);
    }

private:
    static UUnrealMCPTestComponent* FindComponent(const UBlueprint& Blueprint)
    {
        if (Blueprint.SimpleConstructionScript == nullptr) return nullptr;
        for (USCS_Node* Node : Blueprint.SimpleConstructionScript->GetAllNodes())
        {
            if (Node != nullptr && Node->ComponentTemplate != nullptr)
            {
                if (UUnrealMCPTestComponent* Component = Cast<UUnrealMCPTestComponent>(Node->ComponentTemplate))
                {
                    return Component;
                }
            }
        }
        return nullptr;
    }

    bool GetValue(const UObject& Object, int32& OutValue) const
    {
        if (Target == EFixtureTarget::Asset)
        {
            const UUnrealMCPTestAsset* Asset = Cast<UUnrealMCPTestAsset>(&Object);
            if (Asset == nullptr) return false;
            OutValue = Asset->Value;
            return true;
        }
        const UBlueprint* Blueprint = Cast<UBlueprint>(&Object);
        if (Blueprint == nullptr) return false;
        if (Target == EFixtureTarget::Component)
        {
            const UUnrealMCPTestComponent* Component = FindComponent(*Blueprint);
            if (Component == nullptr) return false;
            OutValue = Component->Value;
            return true;
        }
        const FString Prefix = TEXT("UnrealMCPTest:");
        OutValue = Blueprint->BlueprintDescription.StartsWith(Prefix)
            ? FCString::Atoi(*Blueprint->BlueprintDescription.Mid(Prefix.Len())) : 0;
        return true;
    }

    bool SetValue(UObject& Object, int32 Value) const
    {
        if (Target == EFixtureTarget::Asset)
        {
            UUnrealMCPTestAsset* Asset = Cast<UUnrealMCPTestAsset>(&Object);
            if (Asset == nullptr) return false;
            Asset->Modify();
            Asset->Value = Value;
            Asset->GetOutermost()->SetDirtyFlag(false);
            return true;
        }
        UBlueprint* Blueprint = Cast<UBlueprint>(&Object);
        if (Blueprint == nullptr) return false;
        if (Target == EFixtureTarget::Component)
        {
            UUnrealMCPTestComponent* Component = FindComponent(*Blueprint);
            if (Component == nullptr) return false;
            Component->Modify();
            Component->Value = Value;
            Blueprint->GetOutermost()->SetDirtyFlag(false);
            return true;
        }
        Blueprint->Modify();
        Blueprint->BlueprintDescription = FString::Printf(TEXT("UnrealMCPTest:%d"), Value);
        Blueprint->GetOutermost()->SetDirtyFlag(false);
        return true;
    }

    bool ReadValue(
        const UObject& Object,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPExtensionError& OutError) const
    {
        int32 Value = 0;
        if (!GetValue(Object, Value))
        {
            OutError = {TEXT("invalid_asset"), TEXT("The fixture target is unavailable")};
            return false;
        }
        OutResult = MakeShared<FJsonObject>();
        OutResult->SetStringField(TEXT("asset_path"), Object.GetPathName());
        OutResult->SetNumberField(TEXT("value"), Value);
        return true;
    }

    EFixtureTarget Target;
};

FUnrealMCPExtensionContribution Contribution(
    const TCHAR* Id,
    EUnrealMCPExtensionCategory Category,
    EUnrealMCPExtensionAccess Access,
    const TCHAR* Tool,
    const TCHAR* Operation,
    const TCHAR* Family,
    const TCHAR* TargetClass,
    TSharedPtr<IUnrealMCPExtensionHandler> Handler)
{
    FUnrealMCPExtensionContribution Value;
    Value.ContributionId = Id;
    Value.Category = Category;
    Value.Access = Access;
    Value.ToolFamily = Tool;
    Value.Operation = Operation;
    Value.TargetFamily = Family;
    Value.TargetClassPath = TargetClass;
    Value.bAllowDerivedTargetClasses = false;
    Value.RequiredLiveCapability = TEXT("fixture_ready");
    Value.AllowedArgumentFields = Access == EUnrealMCPExtensionAccess::Mutation
        ? TArray<FString>{TEXT("value")} : TArray<FString>{};
    Value.StableLimits.Add(TEXT("value_abs"), 1000000);
    Value.Handler = MoveTemp(Handler);
    return Value;
}
}

class FUnrealMCPTestCompanionModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        const TSharedPtr<IUnrealMCPExtensionHandler> Asset = MakeShared<FFixtureHandler>(EFixtureTarget::Asset);
        const TSharedPtr<IUnrealMCPExtensionHandler> Component = MakeShared<FFixtureHandler>(EFixtureTarget::Component);
        const TSharedPtr<IUnrealMCPExtensionHandler> Existing = MakeShared<FFixtureHandler>(EFixtureTarget::ExistingBlueprint);
        FUnrealMCPCompanionRegistration Registration;
        Registration.PluginName = TEXT("UnrealMCPTestCompanion");
        Registration.ExtensionId = TEXT("unreal-mcp-test");
        Registration.OwningModule = TEXT("UnrealMCPTestCompanion");
        Registration.SemanticVersion = UnrealMCPTestCompanion::Version;
        Registration.CompanionApiVersion = UnrealMCPTestCompanion::CompanionApiVersion;
        Registration.ExtensionSchemaRevision = UnrealMCPTestCompanion::ExtensionSchemaRevision;
        Registration.Contributions = {
            Contribution(TEXT("test_asset_read"), EUnrealMCPExtensionCategory::AssetFamily,
                EUnrealMCPExtensionAccess::Read, TEXT("blueprint_inspect"), TEXT("inspect_test_asset"),
                TEXT("test_asset"), TEXT("/Script/UnrealMCPTestCompanion.UnrealMCPTestAsset"), Asset),
            Contribution(TEXT("test_asset_mutation"), EUnrealMCPExtensionCategory::AssetFamily,
                EUnrealMCPExtensionAccess::Mutation, TEXT("blueprint_default_edit"), TEXT("set_test_asset_value"),
                TEXT("test_asset"), TEXT("/Script/UnrealMCPTestCompanion.UnrealMCPTestAsset"), Asset),
            Contribution(TEXT("test_component_read"), EUnrealMCPExtensionCategory::ComponentFamily,
                EUnrealMCPExtensionAccess::Read, TEXT("blueprint_inspect"), TEXT("inspect_test_component"),
                TEXT("test_component"), TEXT("/Script/Engine.Blueprint"), Component),
            Contribution(TEXT("test_component_mutation"), EUnrealMCPExtensionCategory::ComponentFamily,
                EUnrealMCPExtensionAccess::Mutation, TEXT("blueprint_component_edit"), TEXT("set_test_component_value"),
                TEXT("test_component"), TEXT("/Script/Engine.Blueprint"), Component),
            Contribution(TEXT("test_existing_read"), EUnrealMCPExtensionCategory::ExistingAssetContributor,
                EUnrealMCPExtensionAccess::Read, TEXT("blueprint_inspect"), TEXT("inspect_test_contribution"),
                TEXT("actor"), TEXT("/Script/Engine.Blueprint"), Existing),
            Contribution(TEXT("test_existing_mutation"), EUnrealMCPExtensionCategory::ExistingAssetContributor,
                EUnrealMCPExtensionAccess::Mutation, TEXT("blueprint_default_edit"), TEXT("set_test_contribution_value"),
                TEXT("actor"), TEXT("/Script/Engine.Blueprint"), Existing),
        };
        RegistrationResult = IUnrealMCPModule::Get().RegisterCompanion(Registration, *this);
        if (RegistrationResult.bAccepted)
        {
            CreateCrossProcessFixtures();
        }
    }

    virtual void ShutdownModule() override
    {
        BlueprintFixture.Reset();
        AssetFixture.Reset();
        if (RegistrationResult.bAccepted && IUnrealMCPModule::IsAvailable())
        {
            IUnrealMCPModule::Get().UnregisterCompanion(RegistrationResult.Handle, *this);
        }
    }

private:
    void CreateCrossProcessFixtures()
    {
        UPackage* AssetPackage = CreatePackage(TEXT("/Game/UnrealMCPCompanion/DA_TestAsset"));
        UUnrealMCPTestAsset* Asset = FindObject<UUnrealMCPTestAsset>(
            AssetPackage, TEXT("DA_TestAsset"));
        if (Asset == nullptr)
        {
            Asset = NewObject<UUnrealMCPTestAsset>(AssetPackage, TEXT("DA_TestAsset"),
                RF_Public | RF_Standalone | RF_Transactional);
            FAssetRegistryModule::AssetCreated(Asset);
        }
        AssetFixture.Reset(Asset);
        AssetPackage->SetDirtyFlag(false);

        const FString PackageName = TEXT("/Game/UnrealMCPCompanion/BP_TestActor");
        UPackage* BlueprintPackage = CreatePackage(*PackageName);
        UBlueprint* Blueprint = FindObject<UBlueprint>(BlueprintPackage, TEXT("BP_TestActor"));
        if (Blueprint == nullptr)
        {
            Blueprint = FKismetEditorUtilities::CreateBlueprint(
                AActor::StaticClass(), BlueprintPackage, TEXT("BP_TestActor"), BPTYPE_Normal,
                FName(TEXT("UnrealMCPTestCompanion")));
            if (Blueprint != nullptr && Blueprint->SimpleConstructionScript != nullptr)
            {
                USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(
                    UUnrealMCPTestComponent::StaticClass(), TEXT("TestComponent"));
                Blueprint->SimpleConstructionScript->AddNode(Node);
                FKismetEditorUtilities::CompileBlueprint(Blueprint);
                FAssetRegistryModule::AssetCreated(Blueprint);
            }
        }
        BlueprintFixture.Reset(Blueprint);
        if (BlueprintPackage != nullptr) BlueprintPackage->SetDirtyFlag(false);
    }

    FUnrealMCPRegistrationResult RegistrationResult;
    TStrongObjectPtr<UUnrealMCPTestAsset> AssetFixture;
    TStrongObjectPtr<UBlueprint> BlueprintFixture;
};

IMPLEMENT_MODULE(FUnrealMCPTestCompanionModule, UnrealMCPTestCompanion)
