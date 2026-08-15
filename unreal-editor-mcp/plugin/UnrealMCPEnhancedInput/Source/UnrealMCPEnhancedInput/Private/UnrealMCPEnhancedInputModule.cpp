#include "IUnrealMCPModule.h"
#include "UnrealMCPCompanionApi.h"
#include "UnrealMCPEnhancedInputVersion.h"

#include "EnhancedActionKeyMapping.h"
#include "Engine/Blueprint.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "Misc/SecureHash.h"
#include "PlayerMappableInputConfig.h"
#include "PlayerMappableKeySettings.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

PRAGMA_DISABLE_DEPRECATION_WARNINGS

namespace
{
constexpr TCHAR ActionSection[] = TEXT("input_action");
constexpr TCHAR MappingContextSection[] = TEXT("input_mapping_context");
constexpr TCHAR LegacyConfigSection[] = TEXT("player_mappable_input_config");
constexpr TCHAR TriggerBlueprintSection[] = TEXT("input_trigger_blueprint");
constexpr TCHAR ModifierBlueprintSection[] = TEXT("input_modifier_blueprint");

enum class EEnhancedInputFamily : uint8
{
    Action,
    MappingContext,
    LegacyConfig,
    TriggerBlueprint,
    ModifierBlueprint,
};

FString StableIdentity(const FString& Seed)
{
    return FMD5::HashAnsiString(*Seed).ToLower();
}

void SetError(FUnrealMCPError& OutError, const TCHAR* Code, const TCHAR* Message)
{
    OutError = {Code, Message};
}

template <typename T>
FString EnumName(T Value)
{
    const UEnum* Enum = StaticEnum<T>();
    return Enum != nullptr ? Enum->GetNameStringByValue(static_cast<int64>(Value)) : FString();
}

TSharedRef<FUnrealMCPRecord> ReferenceRecord(const UObject* Object)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("object_path"), Object != nullptr ? Object->GetPathName() : FString());
    Result->SetStringField(TEXT("class_path"),
        Object != nullptr && Object->GetClass() != nullptr
            ? Object->GetClass()->GetPathName() : FString());
    Result->SetBoolField(TEXT("resolved"), Object != nullptr);
    return Result;
}

TSharedRef<FUnrealMCPRecord> PlayerSettingsRecord(const UPlayerMappableKeySettings* Settings)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetBoolField(TEXT("present"), Settings != nullptr);
    if (Settings == nullptr)
    {
        return Result;
    }
    Result->SetStringField(TEXT("name"), Settings->Name.ToString());
    Result->SetStringField(TEXT("display_name"), Settings->DisplayName.ToString());
    Result->SetStringField(TEXT("display_category"), Settings->DisplayCategory.ToString());
    TArray<TSharedPtr<FUnrealMCPValue>> Profiles;
    TArray<FString> ProfileIds = Settings->SupportedKeyProfileIds;
    ProfileIds.Sort();
    if (ProfileIds.Num() > UnrealMCPEnhancedInput::MaxProfiles)
    {
        ProfileIds.SetNum(UnrealMCPEnhancedInput::MaxProfiles);
    }
    for (const FString& Profile : ProfileIds)
    {
        Profiles.Add(MakeShared<FUnrealMCPValueString>(Profile));
    }
    Result->SetArrayField(TEXT("supported_key_profiles"), Profiles);
    Result->SetObjectField(TEXT("metadata"), ReferenceRecord(Settings->Metadata));
    return Result;
}

const TSet<FName>& PersistedNestedPropertyAllowlist()
{
    static const TSet<FName> Names = {
        TEXT("ActuationThreshold"), TEXT("bShouldAlwaysTick"),
        TEXT("bAffectedByTimeDilation"), TEXT("HoldTimeThreshold"),
        TEXT("bIsOneShot"), TEXT("TapReleaseTimeThreshold"), TEXT("RepeatDelay"),
        TEXT("NumberOfTapsWhichTriggerRepeat"), TEXT("bTriggerOnStart"),
        TEXT("Interval"), TEXT("TriggerLimit"), TEXT("ChordAction"),
        TEXT("LinkedAction"), TEXT("SelectedTriggerEvent"),
        TEXT("SmoothingMethod"), TEXT("Speed"), TEXT("EasingExponent"),
        TEXT("LowerThreshold"), TEXT("UpperThreshold"), TEXT("Type"),
        TEXT("Scalar"), TEXT("bX"), TEXT("bY"), TEXT("bZ"),
        TEXT("CurveExponent"), TEXT("ResponseX"), TEXT("ResponseY"),
        TEXT("ResponseZ"), TEXT("FOVScale"), TEXT("FOVScalingType"),
        TEXT("Order")};
    return Names;
}

bool EncodePersistedNestedSettings(
    const UObject& Object,
    const TSharedRef<FUnrealMCPRecord>& OutProperties,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutUnsupported,
    FString& OutFingerprintMaterial,
    FUnrealMCPError& OutError)
{
    const UObject* Defaults = Object.GetClass()->GetDefaultObject(false);
    int32 Encoded = 0;
    int32 Unsupported = 0;
    for (TFieldIterator<FProperty> It(Object.GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        const FProperty* Property = *It;
        if (Property == nullptr || !Property->HasAnyPropertyFlags(CPF_Edit)
            || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
        {
            continue;
        }
        if (!PersistedNestedPropertyAllowlist().Contains(Property->GetFName()))
        {
            if (Unsupported >= UnrealMCPEnhancedInput::MaxUnsupportedProperties)
            {
                SetError(OutError, TEXT("response_too_large"),
                    TEXT("Enhanced Input custom properties exceed the unsupported-data bound"));
                return false;
            }
            OutUnsupported.Add(MakeShared<FUnrealMCPValueString>(Property->GetName()));
            ++Unsupported;
            continue;
        }
        if (Encoded >= UnrealMCPEnhancedInput::MaxPersistedProperties)
        {
            SetError(OutError, TEXT("response_too_large"),
                TEXT("Enhanced Input persisted properties exceed the output bound"));
            return false;
        }
        FString Exported;
        Property->ExportText_InContainer(0, Exported, &Object, Defaults,
            const_cast<UObject*>(&Object), PPF_None);
        if (Exported.Len() * sizeof(TCHAR) > UnrealMCPEnhancedInput::MaxExportedPropertyBytes)
        {
            SetError(OutError, TEXT("response_too_large"),
                TEXT("An Enhanced Input property exceeds the encoded-value bound"));
            return false;
        }
        const TSharedRef<FUnrealMCPRecord> Value = MakeShared<FUnrealMCPRecord>();
        Value->SetStringField(TEXT("type"), Property->GetCPPType());
        Value->SetStringField(TEXT("value"), Exported);
        Value->SetStringField(TEXT("source"),
            Defaults != nullptr && Property->Identical_InContainer(&Object, Defaults)
                ? TEXT("default") : TEXT("local"));
        OutProperties->SetObjectField(Property->GetName(), Value);
        OutFingerprintMaterial += Property->GetName() + TEXT("=") + Exported + TEXT(";");
        ++Encoded;
    }
    OutUnsupported.Sort([](const TSharedPtr<FUnrealMCPValue>& Left,
                           const TSharedPtr<FUnrealMCPValue>& Right)
    {
        FString LeftValue;
        FString RightValue;
        if (Left.IsValid()) Left->TryGetString(LeftValue);
        if (Right.IsValid()) Right->TryGetString(RightValue);
        return LeftValue < RightValue;
    });
    return true;
}

bool NestedObjectRecord(
    const UObject* Object,
    const FString& OwnerIdentity,
    const TCHAR* Kind,
    int32 Index,
    const TSharedRef<FUnrealMCPRecord>& OutRecord,
    FString& OutFingerprintMaterial,
    FUnrealMCPError& OutError)
{
    OutRecord->SetStringField(TEXT("nested_id"), StableIdentity(
        OwnerIdentity + TEXT("|") + Kind + TEXT("|") + LexToString(Index)));
    OutRecord->SetNumberField(TEXT("index"), Index);
    OutRecord->SetStringField(TEXT("kind"), Kind);
    OutRecord->SetBoolField(TEXT("resolved"), Object != nullptr);
    if (Object == nullptr)
    {
        OutRecord->SetStringField(TEXT("class_path"), FString());
        OutRecord->SetBoolField(TEXT("supported"), false);
        OutRecord->SetStringField(TEXT("unsupported_reason"), TEXT("unresolved_instance"));
        OutFingerprintMaterial += FString::Printf(TEXT("%s:%d:null;"), Kind, Index);
        return true;
    }
    OutRecord->SetStringField(TEXT("class_path"), Object->GetClass()->GetPathName());
    const bool bEngineOwned = Object->GetClass()->GetOutermost()->GetName()
        == TEXT("/Script/EnhancedInput");
    const bool bBlueprintOwned = Cast<UBlueprint>(Object->GetClass()->ClassGeneratedBy) != nullptr;
    OutRecord->SetBoolField(TEXT("supported"), bEngineOwned || bBlueprintOwned);
    OutRecord->SetStringField(TEXT("support_kind"),
        bEngineOwned ? TEXT("enhanced_input_builtin")
            : bBlueprintOwned ? TEXT("custom_blueprint") : TEXT("unknown_plugin_subclass"));
    if (const UBlueprint* Blueprint = Cast<UBlueprint>(Object->GetClass()->ClassGeneratedBy))
    {
        OutRecord->SetStringField(TEXT("blueprint_asset"), Blueprint->GetPathName());
    }
    const TSharedRef<FUnrealMCPRecord> Properties = MakeShared<FUnrealMCPRecord>();
    TArray<TSharedPtr<FUnrealMCPValue>> Unsupported;
    if (!EncodePersistedNestedSettings(
            *Object, Properties, Unsupported, OutFingerprintMaterial, OutError))
    {
        return false;
    }
    OutRecord->SetObjectField(TEXT("properties"), Properties);
    OutRecord->SetArrayField(TEXT("unsupported_custom_data"), Unsupported);
    if (!bEngineOwned && !bBlueprintOwned)
    {
        OutRecord->SetStringField(TEXT("unsupported_reason"),
            TEXT("subclass_is_outside_the_fixed_enhanced_input_allowlist"));
    }
    OutFingerprintMaterial += Object->GetClass()->GetPathName() + TEXT(";");
    return true;
}

template <typename T>
bool NestedArray(
    const TArray<TObjectPtr<T>>& Objects,
    const FString& OwnerIdentity,
    const TCHAR* Kind,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutValues,
    FString& OutFingerprintMaterial,
    FUnrealMCPError& OutError)
{
    if (Objects.Num() > UnrealMCPEnhancedInput::MaxNestedObjects)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("Enhanced Input nested objects exceed the output bound"));
        return false;
    }
    for (int32 Index = 0; Index < Objects.Num(); ++Index)
    {
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        if (!NestedObjectRecord(Objects[Index], OwnerIdentity, Kind, Index,
                Record, OutFingerprintMaterial, OutError))
        {
            return false;
        }
        OutValues.Add(MakeShared<FUnrealMCPValueObject>(Record));
    }
    return true;
}

bool BuildActionBlock(
    const UInputAction& Action,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    FString Material = Action.GetPathName();
    OutBlock->SetStringField(TEXT("value_type"), EnumName(Action.ValueType));
    OutBlock->SetStringField(TEXT("accumulation_behavior"), EnumName(Action.AccumulationBehavior));
    OutBlock->SetStringField(TEXT("description"), Action.ActionDescription.ToString());
    OutBlock->SetBoolField(TEXT("trigger_when_paused"), Action.bTriggerWhenPaused);
    OutBlock->SetBoolField(TEXT("consume_input"), Action.bConsumeInput);
    OutBlock->SetBoolField(TEXT("consume_legacy_mappings"), Action.bConsumesActionAndAxisMappings);
    OutBlock->SetBoolField(TEXT("reserve_all_mappings"), Action.bReserveAllMappings);
    OutBlock->SetNumberField(TEXT("legacy_consuming_trigger_events"),
        Action.TriggerEventsThatConsumeLegacyKeys);
    OutBlock->SetObjectField(TEXT("player_mappable_settings"),
        PlayerSettingsRecord(Action.GetPlayerMappableKeySettings()));
    TArray<TSharedPtr<FUnrealMCPValue>> Triggers;
    TArray<TSharedPtr<FUnrealMCPValue>> Modifiers;
    if (!NestedArray(Action.Triggers, Action.GetPathName(), TEXT("trigger"), Triggers,
            Material, OutError)
        || !NestedArray(Action.Modifiers, Action.GetPathName(), TEXT("modifier"), Modifiers,
            Material, OutError))
    {
        return false;
    }
    OutBlock->SetArrayField(TEXT("triggers"), Triggers);
    OutBlock->SetArrayField(TEXT("modifiers"), Modifiers);
    Material += EnumName(Action.ValueType) + TEXT("|") + EnumName(Action.AccumulationBehavior)
        + TEXT("|") + LexToString(Action.bTriggerWhenPaused)
        + LexToString(Action.bConsumeInput) + LexToString(Action.bConsumesActionAndAxisMappings)
        + LexToString(Action.bReserveAllMappings)
        + LexToString(Action.TriggerEventsThatConsumeLegacyKeys)
        + Action.ActionDescription.ToString();
    OutFingerprint = StableIdentity(Material);
    return true;
}

FString MappingSettingBehavior(const FEnhancedActionKeyMapping& Mapping)
{
    const FProperty* Property = FEnhancedActionKeyMapping::StaticStruct()
        ->FindPropertyByName(TEXT("SettingBehavior"));
    FString Value;
    if (Property != nullptr)
    {
        Property->ExportText_InContainer(0, Value, &Mapping, nullptr, nullptr, PPF_None);
    }
    return Value;
}

bool BuildMappingRecord(
    const FEnhancedActionKeyMapping& Mapping,
    const FString& Profile,
    int32 Index,
    const TSharedRef<FUnrealMCPRecord>& OutRecord,
    FString& OutFingerprintMaterial,
    FUnrealMCPError& OutError)
{
    const FString ActionPath = Mapping.Action != nullptr ? Mapping.Action->GetPathName() : FString();
    const FString KeyName = Mapping.Key.GetFName().ToString();
    const FString MappingId = StableIdentity(
        Profile + TEXT("|") + LexToString(Index) + TEXT("|") + ActionPath + TEXT("|") + KeyName);
    OutRecord->SetStringField(TEXT("mapping_id"), MappingId);
    OutRecord->SetStringField(TEXT("profile"), Profile);
    OutRecord->SetNumberField(TEXT("index"), Index);
    OutRecord->SetObjectField(TEXT("action"), ReferenceRecord(Mapping.Action));
    OutRecord->SetStringField(TEXT("key"), KeyName);
    OutRecord->SetBoolField(TEXT("player_mappable"), Mapping.IsPlayerMappable());
    OutRecord->SetStringField(TEXT("setting_behavior"), MappingSettingBehavior(Mapping));
    OutRecord->SetStringField(TEXT("mapping_name"), Mapping.GetMappingName().ToString());
    OutRecord->SetStringField(TEXT("display_name"), Mapping.GetDisplayName().ToString());
    OutRecord->SetStringField(TEXT("display_category"), Mapping.GetDisplayCategory().ToString());
    OutRecord->SetObjectField(TEXT("player_mappable_settings"),
        PlayerSettingsRecord(Mapping.GetPlayerMappableKeySettings()));
    TArray<TSharedPtr<FUnrealMCPValue>> Triggers;
    TArray<TSharedPtr<FUnrealMCPValue>> Modifiers;
    if (!NestedArray(Mapping.Triggers, MappingId, TEXT("trigger"), Triggers,
            OutFingerprintMaterial, OutError)
        || !NestedArray(Mapping.Modifiers, MappingId, TEXT("modifier"), Modifiers,
            OutFingerprintMaterial, OutError))
    {
        return false;
    }
    OutRecord->SetArrayField(TEXT("triggers"), Triggers);
    OutRecord->SetArrayField(TEXT("modifiers"), Modifiers);
    OutFingerprintMaterial += MappingId + TEXT("|") + MappingSettingBehavior(Mapping) + TEXT(";");
    return true;
}

bool AppendMappings(
    const TArray<FEnhancedActionKeyMapping>& Mappings,
    const FString& Profile,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutMappings,
    FString& OutFingerprintMaterial,
    FUnrealMCPError& OutError)
{
    if (OutMappings.Num() + Mappings.Num() > UnrealMCPEnhancedInput::MaxMappings)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("Enhanced Input mappings exceed the output bound"));
        return false;
    }
    for (int32 Index = 0; Index < Mappings.Num(); ++Index)
    {
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        if (!BuildMappingRecord(Mappings[Index], Profile, Index, Record,
                OutFingerprintMaterial, OutError))
        {
            return false;
        }
        OutMappings.Add(MakeShared<FUnrealMCPValueObject>(Record));
    }
    return true;
}

bool BuildMappingContextBlock(
    const UInputMappingContext& Context,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    FString Material = Context.GetPathName();
    OutBlock->SetStringField(TEXT("description"), Context.ContextDescription.ToString());
    OutBlock->SetStringField(TEXT("input_mode_filter"), EnumName(Context.GetInputModeFilterOptions()));
    OutBlock->SetBoolField(TEXT("filters_by_input_mode"), Context.ShouldFilterMappingByInputMode());
    OutBlock->SetStringField(TEXT("input_mode_query"), Context.GetInputModeQuery().GetDescription());
    OutBlock->SetStringField(TEXT("registration_tracking"),
        EnumName(Context.GetRegistrationTrackingMode()));

    TArray<FString> Profiles = Context.GetProfilesWithOverridenMappings();
    Profiles.Sort();
    if (Profiles.Num() > UnrealMCPEnhancedInput::MaxProfiles)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("Enhanced Input mapping profiles exceed the output bound"));
        return false;
    }
    TArray<TSharedPtr<FUnrealMCPValue>> ProfileValues;
    TArray<TSharedPtr<FUnrealMCPValue>> Mappings;
    if (!AppendMappings(Context.GetMappings(), TEXT("default"), Mappings, Material, OutError))
    {
        return false;
    }
    for (const FString& Profile : Profiles)
    {
        ProfileValues.Add(MakeShared<FUnrealMCPValueString>(Profile));
        if (!AppendMappings(Context.GetMappingsForProfile(Profile), Profile,
                Mappings, Material, OutError))
        {
            return false;
        }
    }
    OutBlock->SetArrayField(TEXT("override_profiles"), ProfileValues);
    OutBlock->SetArrayField(TEXT("mappings"), Mappings);
    OutBlock->SetNumberField(TEXT("mapping_count"), Mappings.Num());
    Material += Context.ContextDescription.ToString()
        + EnumName(Context.GetInputModeFilterOptions())
        + Context.GetInputModeQuery().GetDescription()
        + EnumName(Context.GetRegistrationTrackingMode());
    OutFingerprint = StableIdentity(Material);
    return true;
}

bool BuildLegacyConfigBlock(
    const UPlayerMappableInputConfig& Config,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    OutBlock->SetBoolField(TEXT("deprecated_in_ue_5_8"), true);
    OutBlock->SetStringField(TEXT("replacement"), TEXT("UEnhancedInputUserSettings"));
    OutBlock->SetStringField(TEXT("config_name"), Config.GetConfigName().ToString());
    OutBlock->SetStringField(TEXT("display_name"), Config.GetDisplayName().ToString());
    OutBlock->SetBoolField(TEXT("marked_deprecated"), Config.IsDeprecated());
    OutBlock->SetObjectField(TEXT("metadata"), ReferenceRecord(Config.GetMetadata()));

    TArray<TPair<FString, int32>> Contexts;
    for (const TPair<TObjectPtr<UInputMappingContext>, int32>& Entry : Config.GetMappingContexts())
    {
        Contexts.Add({Entry.Key != nullptr ? Entry.Key->GetPathName() : FString(), Entry.Value});
    }
    Contexts.Sort([](const TPair<FString, int32>& Left, const TPair<FString, int32>& Right)
    {
        return Left.Key == Right.Key ? Left.Value < Right.Value : Left.Key < Right.Key;
    });
    if (Contexts.Num() > UnrealMCPEnhancedInput::MaxProfiles)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("Legacy Enhanced Input contexts exceed the output bound"));
        return false;
    }
    FString Material = Config.GetPathName();
    TArray<TSharedPtr<FUnrealMCPValue>> Values;
    for (const TPair<FString, int32>& Entry : Contexts)
    {
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("context_path"), Entry.Key);
        Record->SetNumberField(TEXT("priority"), Entry.Value);
        Record->SetBoolField(TEXT("resolved"), !Entry.Key.IsEmpty());
        Values.Add(MakeShared<FUnrealMCPValueObject>(Record));
        Material += Entry.Key + TEXT("|") + LexToString(Entry.Value) + TEXT(";");
    }
    OutBlock->SetArrayField(TEXT("contexts"), Values);
    Material += Config.GetConfigName().ToString() + Config.GetDisplayName().ToString()
        + LexToString(Config.IsDeprecated());
    OutFingerprint = StableIdentity(Material);
    return true;
}

UObject* ResolveBlueprintDefaults(UObject* Asset, const UClass* RequiredBase)
{
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
    UClass* GeneratedClass = Blueprint != nullptr
        ? (Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass : Blueprint->ParentClass)
        : nullptr;
    return GeneratedClass != nullptr && GeneratedClass->IsChildOf(RequiredBase)
        ? GeneratedClass->GetDefaultObject(false) : nullptr;
}

bool BuildBlueprintBlock(
    UBlueprint& Blueprint,
    const UClass* RequiredBase,
    const TArray<FName>& SupportedFunctions,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    UObject* Defaults = ResolveBlueprintDefaults(&Blueprint, RequiredBase);
    if (Defaults == nullptr)
    {
        SetError(OutError, TEXT("invalid_asset"),
            TEXT("The Blueprint does not represent the selected Enhanced Input class"));
        return false;
    }
    OutBlock->SetStringField(TEXT("generated_class"), Defaults->GetClass()->GetPathName());
    OutBlock->SetStringField(TEXT("native_base"), RequiredBase->GetPathName());
    OutBlock->SetBoolField(TEXT("ordinary_blueprint_semantics_composed"), true);
    OutBlock->SetBoolField(TEXT("runtime_evaluation_excluded"), true);
    TArray<TSharedPtr<FUnrealMCPValue>> Functions;
    FString Material = Blueprint.GetPathName() + Defaults->GetClass()->GetPathName();
    for (const FName FunctionName : SupportedFunctions)
    {
        const UFunction* Function = Defaults->GetClass()->FindFunctionByName(FunctionName);
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("name"), FunctionName.ToString());
        Record->SetBoolField(TEXT("available"), Function != nullptr);
        Record->SetBoolField(TEXT("implemented_by_blueprint"),
            Function != nullptr && Function->GetOuterUClass() == Defaults->GetClass());
        Functions.Add(MakeShared<FUnrealMCPValueObject>(Record));
        Material += FunctionName.ToString()
            + LexToString(Function != nullptr && Function->GetOuterUClass() == Defaults->GetClass());
    }
    OutBlock->SetArrayField(TEXT("supported_functions"), Functions);
    const TSharedRef<FUnrealMCPRecord> Properties = MakeShared<FUnrealMCPRecord>();
    TArray<TSharedPtr<FUnrealMCPValue>> Unsupported;
    if (!EncodePersistedNestedSettings(
            *Defaults, Properties, Unsupported, Material, OutError))
    {
        return false;
    }
    OutBlock->SetObjectField(TEXT("enhanced_input_defaults"), Properties);
    OutBlock->SetArrayField(TEXT("custom_defaults_reported_by_base_blueprint"), Unsupported);
    OutFingerprint = StableIdentity(Material);
    return true;
}

FString SectionName(EEnhancedInputFamily Family)
{
    switch (Family)
    {
    case EEnhancedInputFamily::Action: return ActionSection;
    case EEnhancedInputFamily::MappingContext: return MappingContextSection;
    case EEnhancedInputFamily::LegacyConfig: return LegacyConfigSection;
    case EEnhancedInputFamily::TriggerBlueprint: return TriggerBlueprintSection;
    case EEnhancedInputFamily::ModifierBlueprint: return ModifierBlueprintSection;
    }
    return FString();
}

bool BuildBlock(
    UObject* Asset,
    EEnhancedInputFamily Family,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    switch (Family)
    {
    case EEnhancedInputFamily::Action:
        if (const UInputAction* Action = Cast<UInputAction>(Asset))
            return BuildActionBlock(*Action, OutBlock, OutFingerprint, OutError);
        break;
    case EEnhancedInputFamily::MappingContext:
        if (const UInputMappingContext* Context = Cast<UInputMappingContext>(Asset))
            return BuildMappingContextBlock(*Context, OutBlock, OutFingerprint, OutError);
        break;
    case EEnhancedInputFamily::LegacyConfig:
        if (const UPlayerMappableInputConfig* Config = Cast<UPlayerMappableInputConfig>(Asset))
            return BuildLegacyConfigBlock(*Config, OutBlock, OutFingerprint, OutError);
        break;
    case EEnhancedInputFamily::TriggerBlueprint:
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
            return BuildBlueprintBlock(*Blueprint, UInputTrigger::StaticClass(),
                {TEXT("GetTriggerType"), TEXT("UpdateState"), TEXT("ReceiveTriggerReinstanced")},
                OutBlock, OutFingerprint, OutError);
        break;
    case EEnhancedInputFamily::ModifierBlueprint:
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
            return BuildBlueprintBlock(*Blueprint, UInputModifier::StaticClass(),
                {TEXT("ModifyRaw"), TEXT("GetVisualizationColor"),
                    TEXT("ReceiveModifierReinstanced")},
                OutBlock, OutFingerprint, OutError);
        break;
    }
    SetError(OutError, TEXT("invalid_asset"),
        TEXT("The asset does not represent the selected Enhanced Input family"));
    return false;
}

class FEnhancedInputInspectionAdapter final : public IUnrealMCPAssetFamilyInspectionAdapter
{
public:
    explicit FEnhancedInputInspectionAdapter(EEnhancedInputFamily InFamily) : Family(InFamily) {}

    bool Inspect(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPAssetFamilySelectorRouter& Selectors,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        if (Context.bHasPaging || Context.bHasPartialGraphFlag)
        {
            SetError(OutError, TEXT("invalid_argument"),
                TEXT("Enhanced Input semantic selectors do not support paging or graph flags"));
            return false;
        }
        const FString Section = SectionName(Family);
        if (!Selectors.Register({Section, {Section}, false, false}, OutError)
            || !Selectors.Freeze(OutError))
        {
            return false;
        }
        if (!Context.Selector.IsRoot() && Selectors.Resolve(Context.Selector, OutError) == nullptr)
        {
            return false;
        }
        const TSharedRef<FUnrealMCPRecord> Block = MakeShared<FUnrealMCPRecord>();
        FString Fingerprint;
        if (!BuildBlock(Context.Asset, Family, Block, Fingerprint, OutError))
        {
            return false;
        }
        if (Context.Selector.IsRoot())
        {
            if (!Document.Add({TEXT("selectors"), TEXT("array"),
                MakeShared<FUnrealMCPValueArray>(TArray<TSharedPtr<FUnrealMCPValue>>{
                    MakeShared<FUnrealMCPValueString>(Section)})}, OutError))
            {
                return false;
            }
        }
        else
        {
            const TSharedRef<FUnrealMCPRecord> Selection = MakeShared<FUnrealMCPRecord>();
            Selection->SetStringField(TEXT("selector"), Section);
            Selection->SetStringField(TEXT("kind"), TEXT("record"));
            if (!Document.Add({TEXT("selection"), TEXT("record"),
                MakeShared<FUnrealMCPValueObject>(Selection)}, OutError))
            {
                return false;
            }
        }
        return Document.Add({Section, TEXT("record"),
                MakeShared<FUnrealMCPValueObject>(Block)}, OutError)
            && Snapshot.Add(Section, Fingerprint, OutError);
    }

private:
    EEnhancedInputFamily Family;
};

FUnrealMCPCompanionAssetFamily MakeFamily(
    EEnhancedInputFamily FamilyKind,
    const TCHAR* FamilyId,
    UClass* NativeClass,
    EUnrealMCPAssetFamilyClassPolicy ClassPolicy)
{
    FUnrealMCPCompanionAssetFamily Family;
    const FString Section = SectionName(FamilyKind);
    Family.FamilyId = FamilyId;
    Family.NativeClassPath = NativeClass->GetPathName();
    Family.ClassPolicy = ClassPolicy;
    Family.Priority = 220;
    Family.RequiredModules = {TEXT("EnhancedInput")};
    Family.Bounds.MaxDocumentBytes = 4 * 1024 * 1024;
    Family.Bounds.MaxValueNodes = 65536;
    Family.Limits = {
        {TEXT("mappings"), UnrealMCPEnhancedInput::MaxMappings},
        {TEXT("nested_objects"), UnrealMCPEnhancedInput::MaxNestedObjects},
        {TEXT("persisted_properties"), UnrealMCPEnhancedInput::MaxPersistedProperties},
        {TEXT("profiles"), UnrealMCPEnhancedInput::MaxProfiles},
        {TEXT("unsupported_properties"), UnrealMCPEnhancedInput::MaxUnsupportedProperties},
        {TEXT("exported_property_bytes"), UnrealMCPEnhancedInput::MaxExportedPropertyBytes}};
    Family.Capabilities.bInspection = true;
    Family.SelectorRoutes = {{Section, {Section}, false, false}};
    Family.StableNestedIdentityKinds = {
        TEXT("input_mapping"), TEXT("input_trigger"), TEXT("input_modifier")};
    Family.InspectionAdapter = MakeShared<FEnhancedInputInspectionAdapter>(FamilyKind);
    Family.SnapshotBuilder = [FamilyKind](UObject* Asset)
    {
        const TSharedRef<FUnrealMCPRecord> Block = MakeShared<FUnrealMCPRecord>();
        FString Fingerprint;
        FUnrealMCPError Error;
        return BuildBlock(Asset, FamilyKind, Block, Fingerprint, Error)
            ? Fingerprint : FString();
    };
    return Family;
}

#if WITH_DEV_AUTOMATION_TESTS
void SetPlayerMappableSettings(
    UInputAction& Action,
    const FName Name,
    const FText& DisplayName,
    const FText& DisplayCategory)
{
    UPlayerMappableKeySettings* Settings = NewObject<UPlayerMappableKeySettings>(&Action);
    Settings->Name = Name;
    Settings->DisplayName = DisplayName;
    Settings->DisplayCategory = DisplayCategory;
    FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(
        Action.GetClass(), TEXT("PlayerMappableKeySettings"));
    if (Property != nullptr)
    {
        Property->SetObjectPropertyValue_InContainer(&Action, Settings);
    }
}

bool SaveAsset(UObject* Asset)
{
    if (Asset == nullptr)
    {
        return false;
    }
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    SaveArgs.bSlowTask = false;
    return UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, SaveArgs);
}

bool RemoveFixture(const TCHAR* PackageName)
{
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());
    return !IFileManager::Get().FileExists(*Filename)
        || IFileManager::Get().Delete(*Filename, false, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPEnhancedInputInspectionTest,
    "UnrealMCP.EnhancedInput.AssetInspection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPEnhancedInputInspectionTest::RunTest(const FString& Parameters)
{
    UPackage* Package = CreatePackage(TEXT("/Engine/Transient/UnrealMCPEnhancedInputInspection"));
    UInputAction* Action = NewObject<UInputAction>(Package, TEXT("IA_Test"));
    UInputTriggerHold* Trigger = NewObject<UInputTriggerHold>(Action);
    Trigger->HoldTimeThreshold = 0.75f;
    UInputModifierDeadZone* Modifier = NewObject<UInputModifierDeadZone>(Action);
    Modifier->LowerThreshold = 0.25f;
    Action->Triggers.Add(Trigger);
    Action->Modifiers.Add(Modifier);
    Action->ValueType = EInputActionValueType::Axis2D;
    Action->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;
    SetPlayerMappableSettings(*Action, TEXT("Move"), FText::FromString(TEXT("Move")),
        FText::FromString(TEXT("Movement")));

    const bool bDirtyBefore = Package->IsDirty();
    TSharedRef<FUnrealMCPRecord> First = MakeShared<FUnrealMCPRecord>();
    TSharedRef<FUnrealMCPRecord> Second = MakeShared<FUnrealMCPRecord>();
    FString FirstFingerprint;
    FString SecondFingerprint;
    FUnrealMCPError Error;
    TestTrue(TEXT("Input Action semantic block builds"),
        BuildActionBlock(*Action, First, FirstFingerprint, Error));
    TestTrue(TEXT("Repeated Input Action semantic block builds"),
        BuildActionBlock(*Action, Second, SecondFingerprint, Error));
    TestEqual(TEXT("Input Action inspection is deterministic"),
        FirstFingerprint, SecondFingerprint);
    TestEqual(TEXT("Input Action inspection preserves package dirtiness"),
        Package->IsDirty(), bDirtyBefore);

    UInputMappingContext* Context = NewObject<UInputMappingContext>(Package, TEXT("IMC_Test"));
    FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, EKeys::SpaceBar);
    Mapping.Triggers.Add(NewObject<UInputTriggerPressed>(Context));
    Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
    Context->MapKey(Action, EKeys::SpaceBar);
    TSharedRef<FUnrealMCPRecord> ContextBlock = MakeShared<FUnrealMCPRecord>();
    FString ContextFingerprint;
    TestTrue(TEXT("Input Mapping Context semantic block builds"),
        BuildMappingContextBlock(*Context, ContextBlock, ContextFingerprint, Error));
    TestTrue(TEXT("Mapping Context fingerprint includes ordered mapping state"),
        !ContextFingerprint.IsEmpty());
    double MappingCount = 0.0;
    TestTrue(TEXT("Mapping Context preserves repeated ordered key mappings"),
        ContextBlock->TryGetNumberField(TEXT("mapping_count"), MappingCount)
            && MappingCount == 2.0);

    UPlayerMappableInputConfig* Legacy =
        NewObject<UPlayerMappableInputConfig>(Package, TEXT("PMI_Legacy"));
    const_cast<TMap<TObjectPtr<UInputMappingContext>, int32>&>(
        Legacy->GetMappingContexts()).Add(Context, 7);
    TSharedRef<FUnrealMCPRecord> LegacyBlock = MakeShared<FUnrealMCPRecord>();
    FString LegacyFingerprint;
    TestTrue(TEXT("Legacy player-mappable config semantic block builds"),
        BuildLegacyConfigBlock(*Legacy, LegacyBlock, LegacyFingerprint, Error));
    bool bDeprecated = false;
    TestTrue(TEXT("Legacy family reports UE 5.8 deprecation"),
        LegacyBlock->TryGetBoolField(TEXT("deprecated_in_ue_5_8"), bDeprecated)
            && bDeprecated);

    for (const EInputActionValueType ValueType : {
        EInputActionValueType::Boolean, EInputActionValueType::Axis1D,
        EInputActionValueType::Axis2D, EInputActionValueType::Axis3D})
    {
        Action->ValueType = ValueType;
        TSharedRef<FUnrealMCPRecord> ValueBlock = MakeShared<FUnrealMCPRecord>();
        FString ValueFingerprint;
        TestTrue(FString::Printf(TEXT("Input Action value type %s is inspected"),
            *EnumName(ValueType)), BuildActionBlock(
                *Action, ValueBlock, ValueFingerprint, Error));
    }
    UInputAction* Oversized = NewObject<UInputAction>(Package, TEXT("IA_Oversized"));
    for (int32 Index = 0; Index <= UnrealMCPEnhancedInput::MaxNestedObjects; ++Index)
    {
        Oversized->Triggers.Add(NewObject<UInputTriggerPressed>(Oversized));
    }
    TSharedRef<FUnrealMCPRecord> OversizedBlock = MakeShared<FUnrealMCPRecord>();
    FString OversizedFingerprint;
    FUnrealMCPError OversizedError;
    TestFalse(TEXT("Input Action nested-object overflow fails closed"),
        BuildActionBlock(*Oversized, OversizedBlock, OversizedFingerprint, OversizedError));
    TestEqual(TEXT("Input Action overflow uses the stable bound error"),
        OversizedError.Code, FString(TEXT("response_too_large")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPEnhancedInputLiveFixtureTest,
    "UnrealMCP.EnhancedInput.LiveFixture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPEnhancedInputLiveFixtureTest::RunTest(const FString& Parameters)
{
    const TArray<FString> PackageNames = {
        TEXT("/Game/UnrealMCPEnhancedInput/IA_InspectionFixture"),
        TEXT("/Game/UnrealMCPEnhancedInput/IMC_InspectionFixture"),
        TEXT("/Game/UnrealMCPEnhancedInput/PMI_LegacyFixture"),
        TEXT("/Game/UnrealMCPEnhancedInput/BP_InputTriggerFixture"),
        TEXT("/Game/UnrealMCPEnhancedInput/BP_InputModifierFixture")};
    for (const FString& PackageName : PackageNames)
    {
        if (!TestTrue(TEXT("existing Enhanced Input fixture is removed"),
                RemoveFixture(*PackageName)))
        {
            return false;
        }
    }

    UPackage* ActionPackage = CreatePackage(*PackageNames[0]);
    UInputAction* Action = NewObject<UInputAction>(ActionPackage,
        TEXT("IA_InspectionFixture"), RF_Public | RF_Standalone);
    Action->ValueType = EInputActionValueType::Axis2D;
    SetPlayerMappableSettings(*Action, TEXT("Move"), FText::FromString(TEXT("Move")),
        FText::FromString(TEXT("Movement")));
    UInputTriggerHold* Trigger = NewObject<UInputTriggerHold>(Action);
    Trigger->HoldTimeThreshold = 0.6f;
    Action->Triggers.Add(Trigger);
    Action->Modifiers.Add(NewObject<UInputModifierDeadZone>(Action));
    TestTrue(TEXT("Input Action fixture persists"), SaveAsset(Action));

    UPackage* ContextPackage = CreatePackage(*PackageNames[1]);
    UInputMappingContext* Context = NewObject<UInputMappingContext>(ContextPackage,
        TEXT("IMC_InspectionFixture"), RF_Public | RF_Standalone);
    FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, EKeys::SpaceBar);
    Mapping.Triggers.Add(NewObject<UInputTriggerPressed>(Context));
    Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
    Context->MapKey(Action, EKeys::SpaceBar);
    TestTrue(TEXT("Input Mapping Context fixture persists"), SaveAsset(Context));

    UPackage* LegacyPackage = CreatePackage(*PackageNames[2]);
    UPlayerMappableInputConfig* Legacy = NewObject<UPlayerMappableInputConfig>(
        LegacyPackage, TEXT("PMI_LegacyFixture"), RF_Public | RF_Standalone);
    const_cast<TMap<TObjectPtr<UInputMappingContext>, int32>&>(
        Legacy->GetMappingContexts()).Add(Context, 7);
    TestTrue(TEXT("legacy player-mappable config fixture persists"), SaveAsset(Legacy));

    UPackage* TriggerPackage = CreatePackage(*PackageNames[3]);
    UBlueprint* TriggerBlueprint = FKismetEditorUtilities::CreateBlueprint(
        UInputTrigger::StaticClass(), TriggerPackage, TEXT("BP_InputTriggerFixture"),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    TestNotNull(TEXT("custom Input Trigger Blueprint fixture is created"), TriggerBlueprint);
    if (TriggerBlueprint != nullptr)
    {
        FKismetEditorUtilities::CompileBlueprint(TriggerBlueprint);
    }
    TestTrue(TEXT("custom Input Trigger Blueprint fixture persists"),
        SaveAsset(TriggerBlueprint));

    UPackage* ModifierPackage = CreatePackage(*PackageNames[4]);
    UBlueprint* ModifierBlueprint = FKismetEditorUtilities::CreateBlueprint(
        UInputModifier::StaticClass(), ModifierPackage, TEXT("BP_InputModifierFixture"),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    TestNotNull(TEXT("custom Input Modifier Blueprint fixture is created"), ModifierBlueprint);
    if (ModifierBlueprint != nullptr)
    {
        FKismetEditorUtilities::CompileBlueprint(ModifierBlueprint);
    }
    TestTrue(TEXT("custom Input Modifier Blueprint fixture persists"),
        SaveAsset(ModifierBlueprint));

    UE_LOG(LogTemp, Display, TEXT("UNREAL_MCP_ENHANCED_INPUT_FIXTURES=%s"),
        *FString::Join(PackageNames, TEXT(",")));
    return true;
}
#endif

class FUnrealMCPEnhancedInputModule final : public IModuleInterface
{
public:
    void StartupModule() override
    {
        FUnrealMCPCompanionRegistration Registration;
        Registration.PluginName = TEXT("UnrealMCPEnhancedInput");
        Registration.ExtensionId = TEXT("unreal-mcp-enhanced-input");
        Registration.OwningModule = TEXT("UnrealMCPEnhancedInput");
        Registration.SemanticVersion = UnrealMCPEnhancedInput::Version;
        Registration.CompanionApiVersion = UnrealMCPEnhancedInput::CompanionApiVersion;
        Registration.ExtensionSchemaRevision = UnrealMCPEnhancedInput::ExtensionSchemaRevision;
        Registration.RequiredEnginePlugins = {TEXT("EnhancedInput")};
        Registration.RequiredEngineModules = {TEXT("EnhancedInput")};
        Registration.AssetFamilies = {
            MakeFamily(EEnhancedInputFamily::Action, TEXT("input_action"),
                UInputAction::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact),
            MakeFamily(EEnhancedInputFamily::MappingContext, TEXT("input_mapping_context"),
                UInputMappingContext::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact),
            MakeFamily(EEnhancedInputFamily::LegacyConfig,
                TEXT("player_mappable_input_config"),
                UPlayerMappableInputConfig::StaticClass(), EUnrealMCPAssetFamilyClassPolicy::Exact),
            MakeFamily(EEnhancedInputFamily::TriggerBlueprint,
                TEXT("input_trigger_blueprint"), UInputTrigger::StaticClass(),
                EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived),
            MakeFamily(EEnhancedInputFamily::ModifierBlueprint,
                TEXT("input_modifier_blueprint"), UInputModifier::StaticClass(),
                EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived)};
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

PRAGMA_ENABLE_DEPRECATION_WARNINGS

IMPLEMENT_MODULE(FUnrealMCPEnhancedInputModule, UnrealMCPEnhancedInput)
