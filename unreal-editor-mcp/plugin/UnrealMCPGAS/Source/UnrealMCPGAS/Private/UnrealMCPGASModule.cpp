#include "IUnrealMCPModule.h"
#include "UnrealMCPCompanionApi.h"
#include "UnrealMCPGASVersion.h"

#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"

namespace UnrealMCPGAS
{
FUnrealMCPExtensionContribution MakeGameplayEffectInspectionContribution();
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#endif

namespace
{
constexpr TCHAR OperationName[] = TEXT("inspect_gameplay_ability");

void SetError(FUnrealMCPExtensionError& OutError, const TCHAR* Code, const TCHAR* Message)
{
    OutError.Code = Code;
    OutError.Message = Message;
    OutError.Details = MakeShared<FJsonObject>();
}

FString StableIdentity(const FString& Seed)
{
    return FMD5::HashAnsiString(*Seed).ToLower();
}

FString InstancingPolicyName(EGameplayAbilityInstancingPolicy::Type Value)
{
    switch (Value)
    {
    case static_cast<EGameplayAbilityInstancingPolicy::Type>(0): return TEXT("non_instanced");
    case EGameplayAbilityInstancingPolicy::InstancedPerActor: return TEXT("instanced_per_actor");
    case EGameplayAbilityInstancingPolicy::InstancedPerExecution: return TEXT("instanced_per_execution");
    default: return TEXT("unknown");
    }
}

FString ReplicationPolicyName(EGameplayAbilityReplicationPolicy::Type Value)
{
    switch (Value)
    {
    case EGameplayAbilityReplicationPolicy::ReplicateNo: return TEXT("do_not_replicate");
    case EGameplayAbilityReplicationPolicy::ReplicateYes: return TEXT("replicate");
    default: return TEXT("unknown");
    }
}

FString NetExecutionPolicyName(EGameplayAbilityNetExecutionPolicy::Type Value)
{
    switch (Value)
    {
    case EGameplayAbilityNetExecutionPolicy::LocalPredicted: return TEXT("local_predicted");
    case EGameplayAbilityNetExecutionPolicy::LocalOnly: return TEXT("local_only");
    case EGameplayAbilityNetExecutionPolicy::ServerInitiated: return TEXT("server_initiated");
    case EGameplayAbilityNetExecutionPolicy::ServerOnly: return TEXT("server_only");
    default: return TEXT("unknown");
    }
}

FString NetSecurityPolicyName(EGameplayAbilityNetSecurityPolicy::Type Value)
{
    switch (Value)
    {
    case EGameplayAbilityNetSecurityPolicy::ClientOrServer: return TEXT("client_or_server");
    case EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution: return TEXT("server_only_execution");
    case EGameplayAbilityNetSecurityPolicy::ServerOnlyTermination: return TEXT("server_only_termination");
    case EGameplayAbilityNetSecurityPolicy::ServerOnly: return TEXT("server_only");
    default: return TEXT("unknown");
    }
}

FString TriggerSourceName(EGameplayAbilityTriggerSource::Type Value)
{
    switch (Value)
    {
    case EGameplayAbilityTriggerSource::GameplayEvent: return TEXT("gameplay_event");
    case EGameplayAbilityTriggerSource::OwnedTagAdded: return TEXT("owned_tag_added");
    case EGameplayAbilityTriggerSource::OwnedTagPresent: return TEXT("owned_tag_present");
    default: return TEXT("unknown");
    }
}

const UObject* ParentDefaults(const UGameplayAbility& Ability)
{
    const UClass* SuperClass = Ability.GetClass()->GetSuperClass();
    return SuperClass != nullptr && SuperClass->IsChildOf(UGameplayAbility::StaticClass())
        ? SuperClass->GetDefaultObject(false) : nullptr;
}

FString PropertySource(const UGameplayAbility& Ability, const FProperty* Property)
{
    const UObject* Parent = ParentDefaults(Ability);
    return Property != nullptr && Parent != nullptr
        && Property->Identical_InContainer(&Ability, Parent)
        ? TEXT("inherited") : TEXT("local");
}

TSharedRef<FJsonObject> Scalar(const FString& Value, const FString& Source)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("value"), Value);
    Result->SetStringField(TEXT("source"), Source);
    return Result;
}

TSharedRef<FJsonObject> Boolean(bool Value, const FString& Source)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("value"), Value);
    Result->SetStringField(TEXT("source"), Source);
    return Result;
}

const FProperty* ExactProperty(const UGameplayAbility& Ability, const TCHAR* Name)
{
    return Ability.GetClass()->FindPropertyByName(FName(Name));
}

bool ExactBoolean(const UGameplayAbility& Ability, const TCHAR* Name, bool& OutValue)
{
    const FBoolProperty* Property = FindFProperty<FBoolProperty>(Ability.GetClass(), Name);
    if (Property == nullptr)
    {
        return false;
    }
    OutValue = Property->GetPropertyValue_InContainer(&Ability);
    return true;
}

const FGameplayTagContainer* ExactTagContainer(
    const UGameplayAbility& Ability,
    const TCHAR* Name,
    const FProperty*& OutProperty)
{
    const FStructProperty* Property = FindFProperty<FStructProperty>(Ability.GetClass(), Name);
    OutProperty = Property;
    if (Property == nullptr || Property->Struct != FGameplayTagContainer::StaticStruct())
    {
        return nullptr;
    }
    return Property->ContainerPtrToValuePtr<FGameplayTagContainer>(&Ability);
}

TSharedRef<FJsonObject> EncodeTags(
    const UGameplayAbility& Ability,
    const FGameplayTagContainer& Container,
    const FProperty* Property)
{
    TArray<FGameplayTag> Tags;
    Container.GetGameplayTagArray(Tags);
    Tags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
    {
        return Left.ToString() < Right.ToString();
    });
    TArray<TSharedPtr<FJsonValue>> Values;
    const int32 Count = FMath::Min(Tags.Num(), UnrealMCPGAS::MaxTagsPerContainer);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        Values.Add(MakeShared<FJsonValueString>(Tags[Index].ToString()));
    }
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetArrayField(TEXT("tags"), Values);
    Result->SetNumberField(TEXT("tag_count"), Tags.Num());
    Result->SetBoolField(TEXT("truncated"), Tags.Num() > Count);
    Result->SetStringField(TEXT("source"), PropertySource(Ability, Property));
    return Result;
}

TSharedRef<FJsonObject> EncodeEffectReference(
    const UGameplayAbility& Ability,
    const TCHAR* PropertyName)
{
    const FClassProperty* Property = FindFProperty<FClassProperty>(Ability.GetClass(), PropertyName);
    UClass* EffectClass = Property != nullptr
        ? Cast<UClass>(Property->GetObjectPropertyValue_InContainer(&Ability)) : nullptr;
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("resolved"), EffectClass != nullptr);
    Result->SetStringField(TEXT("class_path"), EffectClass != nullptr ? EffectClass->GetPathName() : FString());
    FString AssetPath;
    if (EffectClass != nullptr)
    {
        if (const UBlueprint* Blueprint = Cast<UBlueprint>(EffectClass->ClassGeneratedBy))
        {
            AssetPath = Blueprint->GetPathName();
        }
    }
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("source"), PropertySource(Ability, Property));
    return Result;
}

bool BuildPayload(
    const UGameplayAbility& Ability,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    FString& OutFingerprint,
    FUnrealMCPExtensionError& OutError)
{
    const TSharedRef<FJsonObject> Policies = MakeShared<FJsonObject>();
    Policies->SetStringField(TEXT("section"), TEXT("gameplay_ability_policies"));
    Policies->SetObjectField(TEXT("instancing_policy"), Scalar(
        InstancingPolicyName(Ability.GetInstancingPolicy()),
        PropertySource(Ability, ExactProperty(Ability, TEXT("InstancingPolicy")))));
    Policies->SetObjectField(TEXT("replication_policy"), Scalar(
        ReplicationPolicyName(Ability.GetReplicationPolicy()),
        PropertySource(Ability, ExactProperty(Ability, TEXT("ReplicationPolicy")))));
    Policies->SetObjectField(TEXT("net_execution_policy"), Scalar(
        NetExecutionPolicyName(Ability.GetNetExecutionPolicy()),
        PropertySource(Ability, ExactProperty(Ability, TEXT("NetExecutionPolicy")))));
    Policies->SetObjectField(TEXT("net_security_policy"), Scalar(
        NetSecurityPolicyName(Ability.GetNetSecurityPolicy()),
        PropertySource(Ability, ExactProperty(Ability, TEXT("NetSecurityPolicy")))));
    for (const TPair<const TCHAR*, const TCHAR*>& Field : {
        TPair<const TCHAR*, const TCHAR*>(TEXT("server_respects_remote_cancellation"), TEXT("bServerRespectsRemoteAbilityCancellation")),
        {TEXT("retrigger_instanced_ability"), TEXT("bRetriggerInstancedAbility")},
        {TEXT("replicate_input_directly"), TEXT("bReplicateInputDirectly")}})
    {
        bool Value = false;
        if (!ExactBoolean(Ability, Field.Value, Value))
        {
            SetError(OutError, TEXT("extension_contract_violation"),
                TEXT("A required Gameplay Ability policy property is unavailable"));
            return false;
        }
        Policies->SetObjectField(Field.Key, Boolean(
            Value, PropertySource(Ability, ExactProperty(Ability, Field.Value))));
    }
    OutRecords.Add(MakeShared<FJsonValueObject>(Policies));

    const TSharedRef<FJsonObject> Tags = MakeShared<FJsonObject>();
    Tags->SetStringField(TEXT("section"), TEXT("gameplay_ability_tags"));
    {
        const FProperty* AssetTagsProperty = ExactProperty(Ability, TEXT("AbilityTags"));
        if (Ability.GetAssetTags().Num() > UnrealMCPGAS::MaxTagScan)
        {
            SetError(OutError, TEXT("response_too_large"),
                TEXT("A Gameplay Ability tag container exceeds the scan bound"));
            return false;
        }
        Tags->SetObjectField(TEXT("asset"), EncodeTags(
            Ability, Ability.GetAssetTags(), AssetTagsProperty));
    }
    for (const TPair<const TCHAR*, const TCHAR*>& Field : {
        TPair<const TCHAR*, const TCHAR*>(TEXT("cancel_abilities"), TEXT("CancelAbilitiesWithTag")),
        {TEXT("block_abilities"), TEXT("BlockAbilitiesWithTag")},
        {TEXT("activation_owned"), TEXT("ActivationOwnedTags")},
        {TEXT("activation_required"), TEXT("ActivationRequiredTags")},
        {TEXT("activation_blocked"), TEXT("ActivationBlockedTags")},
        {TEXT("source_required"), TEXT("SourceRequiredTags")},
        {TEXT("source_blocked"), TEXT("SourceBlockedTags")},
        {TEXT("target_required"), TEXT("TargetRequiredTags")},
        {TEXT("target_blocked"), TEXT("TargetBlockedTags")}})
    {
        const FProperty* Property = nullptr;
        const FGameplayTagContainer* Container = ExactTagContainer(Ability, Field.Value, Property);
        if (Container == nullptr)
        {
            SetError(OutError, TEXT("extension_contract_violation"),
                TEXT("A required Gameplay Ability tag container is unavailable"));
            return false;
        }
        if (Container->Num() > UnrealMCPGAS::MaxTagScan)
        {
            SetError(OutError, TEXT("response_too_large"),
                TEXT("A Gameplay Ability tag container exceeds the scan bound"));
            return false;
        }
        Tags->SetObjectField(Field.Key, EncodeTags(Ability, *Container, Property));
    }
    OutRecords.Add(MakeShared<FJsonValueObject>(Tags));

    const FArrayProperty* TriggerProperty =
        FindFProperty<FArrayProperty>(Ability.GetClass(), TEXT("AbilityTriggers"));
    const FStructProperty* TriggerInner = TriggerProperty != nullptr
        ? CastField<FStructProperty>(TriggerProperty->Inner) : nullptr;
    if (TriggerProperty == nullptr || TriggerInner == nullptr
        || TriggerInner->Struct != FAbilityTriggerData::StaticStruct())
    {
        SetError(OutError, TEXT("extension_contract_violation"),
            TEXT("The Gameplay Ability trigger property is unavailable or incompatible"));
        return false;
    }
    FScriptArrayHelper TriggerArray(TriggerProperty,
        TriggerProperty->ContainerPtrToValuePtr<void>(&Ability));
    if (TriggerArray.Num() > UnrealMCPGAS::MaxTriggerScan)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("Gameplay Ability triggers exceed the scan bound"));
        return false;
    }
    const TSharedRef<FJsonObject> Triggers = MakeShared<FJsonObject>();
    Triggers->SetStringField(TEXT("section"), TEXT("gameplay_ability_triggers"));
    Triggers->SetStringField(TEXT("source"), PropertySource(Ability, TriggerProperty));
    Triggers->SetNumberField(TEXT("trigger_count"), TriggerArray.Num());
    const int32 TriggerCount = FMath::Min(TriggerArray.Num(), UnrealMCPGAS::MaxAbilityTriggers);
    Triggers->SetBoolField(TEXT("truncated"), TriggerArray.Num() > TriggerCount);
    TMap<FString, int32> DuplicateCounts;
    TArray<TSharedPtr<FJsonValue>> TriggerValues;
    for (int32 Index = 0; Index < TriggerCount; ++Index)
    {
        const FAbilityTriggerData* Trigger =
            reinterpret_cast<const FAbilityTriggerData*>(TriggerArray.GetRawPtr(Index));
        const FString Tag = Trigger->TriggerTag.ToString();
        const FString Source = TriggerSourceName(Trigger->TriggerSource);
        const FString Key = Tag + TEXT("|") + Source;
        const int32 Duplicate = DuplicateCounts.FindOrAdd(Key)++;
        const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("trigger_id"), StableIdentity(Key + TEXT("|") + FString::FromInt(Duplicate)));
        Value->SetStringField(TEXT("tag"), Tag);
        Value->SetStringField(TEXT("source"), Source);
        TriggerValues.Add(MakeShared<FJsonValueObject>(Value));
    }
    Triggers->SetArrayField(TEXT("triggers"), TriggerValues);
    OutRecords.Add(MakeShared<FJsonValueObject>(Triggers));

    const TSharedRef<FJsonObject> Effects = MakeShared<FJsonObject>();
    Effects->SetStringField(TEXT("section"), TEXT("gameplay_ability_effects"));
    Effects->SetObjectField(TEXT("cost"), EncodeEffectReference(Ability, TEXT("CostGameplayEffectClass")));
    Effects->SetObjectField(TEXT("cooldown"), EncodeEffectReference(Ability, TEXT("CooldownGameplayEffectClass")));
    OutRecords.Add(MakeShared<FJsonValueObject>(Effects));

    TArray<FString> Fingerprint;
    for (const TCHAR* PropertyName : {
        TEXT("ReplicationPolicy"), TEXT("InstancingPolicy"), TEXT("bServerRespectsRemoteAbilityCancellation"),
        TEXT("bRetriggerInstancedAbility"), TEXT("NetExecutionPolicy"), TEXT("NetSecurityPolicy"),
        TEXT("CostGameplayEffectClass"), TEXT("AbilityTriggers"), TEXT("CooldownGameplayEffectClass"),
        TEXT("AbilityTags"), TEXT("bReplicateInputDirectly"), TEXT("CancelAbilitiesWithTag"),
        TEXT("BlockAbilitiesWithTag"), TEXT("ActivationOwnedTags"), TEXT("ActivationRequiredTags"),
        TEXT("ActivationBlockedTags"), TEXT("SourceRequiredTags"), TEXT("SourceBlockedTags"),
        TEXT("TargetRequiredTags"), TEXT("TargetBlockedTags")})
    {
        const FProperty* Property = ExactProperty(Ability, PropertyName);
        if (Property == nullptr)
        {
            SetError(OutError, TEXT("extension_contract_violation"),
                TEXT("A required Gameplay Ability fingerprint property is unavailable"));
            return false;
        }
        FString Encoded;
        Property->ExportText_InContainer(0, Encoded, &Ability, ParentDefaults(Ability),
            const_cast<UGameplayAbility*>(&Ability), PPF_None);
        Fingerprint.Add(FString(PropertyName) + TEXT("|") + Encoded);
    }
    OutFingerprint = FString::Join(Fingerprint, TEXT("\n"));
    return true;
}

class FGameplayAbilityInspectionHandler final : public IUnrealMCPExtensionHandler
{
public:
    bool IsReady(FString& OutUnavailableReason) const override
    {
        OutUnavailableReason.Reset();
        return UGameplayAbility::StaticClass() != nullptr && UGameplayEffect::StaticClass() != nullptr;
    }

    bool SupportsTarget(const UObject& Target) const override
    {
        return Target.IsA<UGameplayAbility>();
    }

    bool ValidateArguments(
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Arguments,
        FUnrealMCPExtensionError& OutError) const override
    {
        FString Mode;
        if (Operation != OperationName || !Arguments.IsValid()
            || !Arguments->TryGetStringField(TEXT("mode"), Mode) || Mode != TEXT("inspect"))
        {
            SetError(OutError, TEXT("invalid_argument"),
                TEXT("Gameplay Ability inspection requires the exact inspect operation"));
            return false;
        }
        return true;
    }

    bool Inspect(
        const UObject& Target,
        const FString& Operation,
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPExtensionError& OutError) override
    {
        const UGameplayAbility* Ability = Cast<UGameplayAbility>(&Target);
        TArray<TSharedPtr<FJsonValue>> Records;
        FString Fingerprint;
        if (Ability == nullptr || !BuildPayload(*Ability, Records, Fingerprint, OutError))
        {
            if (Ability == nullptr)
            {
                SetError(OutError, TEXT("invalid_asset"),
                    TEXT("The target is not a Gameplay Ability class default object"));
            }
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* RequestedSections = nullptr;
        if (Arguments->TryGetArrayField(TEXT("sections"), RequestedSections)
            && RequestedSections != nullptr)
        {
            const bool bGameplayAbilityRequested = RequestedSections->ContainsByPredicate(
                [](const TSharedPtr<FJsonValue>& Value)
                {
                    FString Section;
                    return Value.IsValid() && Value->TryGetString(Section)
                        && Section == TEXT("gameplay_ability");
                });
            if (!bGameplayAbilityRequested)
            {
                Records.Reset();
            }
        }
        const TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
        Capabilities->SetBoolField(TEXT("inspection"), true);
        Capabilities->SetBoolField(TEXT("mutation"), false);
        Capabilities->SetNumberField(TEXT("max_tags_per_container"), UnrealMCPGAS::MaxTagsPerContainer);
        Capabilities->SetNumberField(TEXT("max_triggers"), UnrealMCPGAS::MaxAbilityTriggers);
        Capabilities->SetNumberField(TEXT("max_tag_scan"), UnrealMCPGAS::MaxTagScan);
        Capabilities->SetNumberField(TEXT("max_trigger_scan"), UnrealMCPGAS::MaxTriggerScan);
        OutResult = MakeShared<FJsonObject>();
        OutResult->SetArrayField(TEXT("records"), Records);
        OutResult->SetObjectField(TEXT("family_capabilities"), Capabilities);
        return true;
    }

    bool AppendFingerprint(
        const UObject& Target,
        const FString& Operation,
        FString& OutFingerprint,
        FUnrealMCPExtensionError& OutError) const override
    {
        const UGameplayAbility* Ability = Cast<UGameplayAbility>(&Target);
        TArray<TSharedPtr<FJsonValue>> Records;
        return Ability != nullptr && BuildPayload(*Ability, Records, OutFingerprint, OutError);
    }

    bool ApplyMutation(
        UObject&, const FString&, const TSharedPtr<FJsonObject>&,
        TSharedPtr<FJsonObject>&, FUnrealMCPExtensionError& OutError) override
    {
        SetError(OutError, TEXT("extension_unavailable"),
            TEXT("The GAS companion is inspection-only"));
        return false;
    }

    bool ReadBack(
        const UObject&, const FString&, const TSharedPtr<FJsonObject>&,
        TSharedPtr<FJsonObject>&, FUnrealMCPExtensionError& OutError) const override
    {
        SetError(OutError, TEXT("extension_unavailable"),
            TEXT("The GAS companion exposes no mutation read-back"));
        return false;
    }
};

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPGASAbilityInspectionTest,
    "UnrealMCP.GAS.AbilityBlueprintInspection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPGASAbilityInspectionTest::RunTest(const FString& Parameters)
{
    UPackage* Package = CreatePackage(TEXT("/Engine/Transient/UnrealMCPGASInspectionTest"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        UGameplayAbility::StaticClass(), Package, TEXT("GA_InspectionTest"),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    TestNotNull(TEXT("Gameplay Ability Blueprint is created"), Blueprint);
    if (Blueprint == nullptr)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    UGameplayAbility* Defaults = Blueprint->GeneratedClass != nullptr
        ? Cast<UGameplayAbility>(Blueprint->GeneratedClass->GetDefaultObject(false)) : nullptr;
    TestNotNull(TEXT("Gameplay Ability defaults resolve"), Defaults);
    if (Defaults == nullptr)
    {
        return false;
    }
    FBoolProperty* Retrigger = FindFProperty<FBoolProperty>(
        Defaults->GetClass(), TEXT("bRetriggerInstancedAbility"));
    TestNotNull(TEXT("Exact activation policy property resolves"), Retrigger);
    if (Retrigger != nullptr)
    {
        Retrigger->SetPropertyValue_InContainer(Defaults, true);
    }
    FArrayProperty* TriggerProperty = FindFProperty<FArrayProperty>(
        Defaults->GetClass(), TEXT("AbilityTriggers"));
    TestNotNull(TEXT("Exact trigger property resolves"), TriggerProperty);
    if (TriggerProperty != nullptr)
    {
        FScriptArrayHelper Triggers(
            TriggerProperty, TriggerProperty->ContainerPtrToValuePtr<void>(Defaults));
        const int32 Index = Triggers.AddValue();
        FAbilityTriggerData* Trigger =
            reinterpret_cast<FAbilityTriggerData*>(Triggers.GetRawPtr(Index));
        Trigger->TriggerSource = EGameplayAbilityTriggerSource::OwnedTagAdded;
    }
    const bool bDirtyBefore = Package->IsDirty();
    TArray<TSharedPtr<FJsonValue>> FirstRecords;
    FString FirstFingerprint;
    FUnrealMCPExtensionError Error;
    TestTrue(TEXT("Typed Gameplay Ability payload builds"),
        BuildPayload(*Defaults, FirstRecords, FirstFingerprint, Error));
    TArray<TSharedPtr<FJsonValue>> SecondRecords;
    FString SecondFingerprint;
    TestTrue(TEXT("Repeated typed inspection succeeds"),
        BuildPayload(*Defaults, SecondRecords, SecondFingerprint, Error));
    TestEqual(TEXT("All typed sections are returned"),
        FirstRecords.Num(), UnrealMCPGAS::MaxInspectionRecords);
    TestEqual(TEXT("Inspection fingerprint is deterministic"),
        FirstFingerprint, SecondFingerprint);
    TestEqual(TEXT("Inspection preserves package dirtiness"),
        Package->IsDirty(), bDirtyBefore);
    TestTrue(TEXT("Fingerprint includes typed state"), !FirstFingerprint.IsEmpty());
    return true;
}
#endif

class FUnrealMCPGASModule final : public IModuleInterface
{
public:
    void StartupModule() override
    {
        FUnrealMCPExtensionContribution AbilityContribution;
        AbilityContribution.ContributionId = TEXT("gameplay_ability_inspection");
        AbilityContribution.Category = EUnrealMCPExtensionCategory::AssetFamily;
        AbilityContribution.Access = EUnrealMCPExtensionAccess::Read;
        AbilityContribution.Persistence = EUnrealMCPExtensionPersistence::None;
        AbilityContribution.ToolFamily = TEXT("blueprint_inspect");
        AbilityContribution.Operation = OperationName;
        AbilityContribution.TargetFamily = TEXT("gameplay_ability");
        AbilityContribution.TargetClassPath = UGameplayAbility::StaticClass()->GetPathName();
        AbilityContribution.bAllowDerivedTargetClasses = true;
        AbilityContribution.RequiredLiveCapability = TEXT("gas_ability_blueprints_inspection");
        AbilityContribution.AllowedArgumentFields = {
            TEXT("mode"), TEXT("sections"), TEXT("graph_id"), TEXT("component_id"),
            TEXT("member_id"), TEXT("function_id"), TEXT("local_id"), TEXT("macro_id"),
            TEXT("custom_event_id"), TEXT("widget_id"), TEXT("property_names"),
            TEXT("include_inherited"), TEXT("page_size")};
        AbilityContribution.StableLimits = {
            {TEXT("records"), UnrealMCPGAS::MaxInspectionRecords},
            {TEXT("tags_per_container"), UnrealMCPGAS::MaxTagsPerContainer},
            {TEXT("triggers"), UnrealMCPGAS::MaxAbilityTriggers},
            {TEXT("tag_scan"), UnrealMCPGAS::MaxTagScan},
            {TEXT("trigger_scan"), UnrealMCPGAS::MaxTriggerScan}};
        AbilityContribution.Handler = MakeShared<FGameplayAbilityInspectionHandler>();

        FUnrealMCPCompanionRegistration Registration;
        Registration.PluginName = TEXT("UnrealMCPGAS");
        Registration.ExtensionId = TEXT("unreal-mcp-gas");
        Registration.OwningModule = TEXT("UnrealMCPGAS");
        Registration.SemanticVersion = UnrealMCPGAS::Version;
        Registration.CompanionApiVersion = UnrealMCPGAS::CompanionApiVersion;
        Registration.ExtensionSchemaRevision = UnrealMCPGAS::ExtensionSchemaRevision;
        Registration.RequiredEnginePlugins = {TEXT("GameplayAbilities")};
        // GameplayTags and GameplayTasks remain direct link dependencies, but Unreal does not
        // necessarily register either transitive runtime module as explicitly loaded. The
        // owning GameplayAbilities module is the authoritative live-module admission gate.
        Registration.RequiredEngineModules = {TEXT("GameplayAbilities")};
        Registration.Contributions.Add(MoveTemp(AbilityContribution));
        Registration.Contributions.Add(UnrealMCPGAS::MakeGameplayEffectInspectionContribution());
        RegistrationResult = IUnrealMCPModule::Get().RegisterCompanion(Registration, *this);
    }

    void ShutdownModule() override
    {
        if (RegistrationResult.bAccepted && IUnrealMCPModule::IsAvailable())
        {
            IUnrealMCPModule::Get().UnregisterCompanion(RegistrationResult.Handle, *this);
        }
        RegistrationResult = {};
    }

private:
    FUnrealMCPRegistrationResult RegistrationResult;
};
}

IMPLEMENT_MODULE(FUnrealMCPGASModule, UnrealMCPGAS)
