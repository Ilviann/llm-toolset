#include "IUnrealMCPModule.h"
#include "UnrealMCPCompanionApi.h"
#include "UnrealMCPCommonUIVersion.h"

#include "CommonActivatableWidget.h"
#include "CommonUserWidget.h"
#include "Dom/JsonObject.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#endif

namespace
{
constexpr TCHAR OperationName[] = TEXT("inspect_commonui_widget");
constexpr TCHAR WidgetSection[] = TEXT("commonui_widget");
constexpr TCHAR ActivationSection[] = TEXT("commonui_activation");
constexpr TCHAR ReferenceSection[] = TEXT("commonui_references");

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

const UObject* ParentDefaults(const UCommonUserWidget& Widget)
{
    const UClass* SuperClass = Widget.GetClass()->GetSuperClass();
    return SuperClass != nullptr && SuperClass->IsChildOf(UCommonUserWidget::StaticClass())
        ? SuperClass->GetDefaultObject(false) : nullptr;
}

const FProperty* ExactProperty(const UCommonUserWidget& Widget, const TCHAR* Name)
{
    return Widget.GetClass()->FindPropertyByName(FName(Name));
}

FString PropertySource(const UCommonUserWidget& Widget, const FProperty* Property)
{
    const UObject* Parent = ParentDefaults(Widget);
    return Property != nullptr && Parent != nullptr
        && Property->Identical_InContainer(&Widget, Parent)
        ? TEXT("inherited") : TEXT("local");
}

TSharedRef<FJsonObject> Boolean(bool Value, const FString& Source)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("value"), Value);
    Result->SetStringField(TEXT("source"), Source);
    return Result;
}

TSharedRef<FJsonObject> Scalar(const FString& Value, const FString& Source)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("value"), Value);
    Result->SetStringField(TEXT("source"), Source);
    return Result;
}

TSharedRef<FJsonObject> Integer(int32 Value, const FString& Source)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("value"), Value);
    Result->SetStringField(TEXT("source"), Source);
    return Result;
}

bool ExactBoolean(
    const UCommonUserWidget& Widget,
    const TCHAR* Name,
    TSharedRef<FJsonObject>& OutValue)
{
    const FBoolProperty* Property = FindFProperty<FBoolProperty>(Widget.GetClass(), Name);
    if (Property == nullptr)
    {
        return false;
    }
    OutValue = Boolean(
        Property->GetPropertyValue_InContainer(&Widget), PropertySource(Widget, Property));
    return true;
}

bool ExactInteger(
    const UCommonUserWidget& Widget,
    const TCHAR* Name,
    TSharedRef<FJsonObject>& OutValue)
{
    const FIntProperty* Property = FindFProperty<FIntProperty>(Widget.GetClass(), Name);
    if (Property == nullptr)
    {
        return false;
    }
    OutValue = Integer(
        Property->GetPropertyValue_InContainer(&Widget), PropertySource(Widget, Property));
    return true;
}

bool ExactText(
    const UCommonUserWidget& Widget,
    const TCHAR* Name,
    TSharedRef<FJsonObject>& OutValue)
{
    const FTextProperty* Property = FindFProperty<FTextProperty>(Widget.GetClass(), Name);
    if (Property == nullptr)
    {
        return false;
    }
    OutValue = Scalar(
        Property->GetPropertyValue_InContainer(&Widget).ToString(), PropertySource(Widget, Property));
    return true;
}

bool ExactExported(
    const UCommonUserWidget& Widget,
    const TCHAR* Name,
    TSharedRef<FJsonObject>& OutValue)
{
    const FProperty* Property = ExactProperty(Widget, Name);
    if (Property == nullptr)
    {
        return false;
    }
    FString Encoded;
    Property->ExportText_InContainer(
        0, Encoded, &Widget, ParentDefaults(Widget), const_cast<UCommonUserWidget*>(&Widget), PPF_None);
    OutValue = Scalar(Encoded, PropertySource(Widget, Property));
    return true;
}

TSharedRef<FJsonObject> EmptyReference(
    const UCommonUserWidget& Widget,
    const FProperty* Property,
    const TCHAR* PropertyName)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("reference_id"), StableIdentity(FString(PropertyName) + TEXT("|")));
    Result->SetStringField(TEXT("object_path"), FString());
    Result->SetStringField(TEXT("class_path"), FString());
    Result->SetBoolField(TEXT("resolved"), false);
    Result->SetStringField(TEXT("source"), PropertySource(Widget, Property));
    return Result;
}

bool HardReference(
    const UCommonUserWidget& Widget,
    const TCHAR* Name,
    TSharedRef<FJsonObject>& OutValue)
{
    const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Widget.GetClass(), Name);
    if (Property == nullptr)
    {
        return false;
    }
    UObject* Object = Property->GetObjectPropertyValue_InContainer(&Widget);
    OutValue = EmptyReference(Widget, Property, Name);
    if (Object != nullptr)
    {
        const FString Path = Object->GetPathName();
        OutValue->SetStringField(TEXT("reference_id"), StableIdentity(FString(Name) + TEXT("|") + Path));
        OutValue->SetStringField(TEXT("object_path"), Path);
        OutValue->SetStringField(TEXT("class_path"), Object->GetClass()->GetPathName());
        OutValue->SetBoolField(TEXT("resolved"), true);
    }
    return true;
}

bool SoftReference(
    const UCommonUserWidget& Widget,
    const TCHAR* Name,
    TSharedRef<FJsonObject>& OutValue)
{
    const FSoftObjectProperty* Property = FindFProperty<FSoftObjectProperty>(Widget.GetClass(), Name);
    if (Property == nullptr)
    {
        return false;
    }
    const FSoftObjectPtr* Value = Property->ContainerPtrToValuePtr<FSoftObjectPtr>(&Widget);
    const FString Path = Value != nullptr ? Value->ToSoftObjectPath().ToString() : FString();
    UObject* Object = Value != nullptr ? Value->Get() : nullptr;
    OutValue = EmptyReference(Widget, Property, Name);
    OutValue->SetStringField(TEXT("reference_id"), StableIdentity(FString(Name) + TEXT("|") + Path));
    OutValue->SetStringField(TEXT("object_path"), Path);
    OutValue->SetStringField(TEXT("class_path"), Object != nullptr ? Object->GetClass()->GetPathName() : FString());
    OutValue->SetBoolField(TEXT("resolved"), Object != nullptr);
    return true;
}

bool AddBoolean(
    const UCommonUserWidget& Widget,
    const TSharedRef<FJsonObject>& Record,
    const TCHAR* Field,
    const TCHAR* Property,
    FUnrealMCPExtensionError& OutError)
{
    TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
    if (!ExactBoolean(Widget, Property, Value))
    {
        SetError(OutError, TEXT("extension_contract_violation"),
            TEXT("A required CommonUI boolean property is unavailable"));
        return false;
    }
    Record->SetObjectField(Field, Value);
    return true;
}

bool BuildPayload(
    const UCommonUserWidget& Widget,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    FString& OutFingerprint,
    FUnrealMCPExtensionError& OutError)
{
    const TSharedRef<FJsonObject> WidgetRecord = MakeShared<FJsonObject>();
    WidgetRecord->SetStringField(TEXT("section"), WidgetSection);
    if (!AddBoolean(Widget, WidgetRecord, TEXT("display_in_action_bar"),
            TEXT("bDisplayInActionBar"), OutError)
        || !AddBoolean(Widget, WidgetRecord, TEXT("consume_pointer_input"),
            TEXT("bConsumePointerInput"), OutError))
    {
        return false;
    }
    OutRecords.Add(MakeShared<FJsonValueObject>(WidgetRecord));

    const UCommonActivatableWidget* Activatable = Cast<UCommonActivatableWidget>(&Widget);
    const TSharedRef<FJsonObject> Activation = MakeShared<FJsonObject>();
    Activation->SetStringField(TEXT("section"), ActivationSection);
    Activation->SetBoolField(TEXT("supported"), Activatable != nullptr);
    const TSharedRef<FJsonObject> References = MakeShared<FJsonObject>();
    References->SetStringField(TEXT("section"), ReferenceSection);
    References->SetBoolField(TEXT("supported"), Activatable != nullptr);
    if (Activatable == nullptr)
    {
        Activation->SetStringField(TEXT("unavailable_reason"), TEXT("not_activatable_widget"));
        References->SetStringField(TEXT("unavailable_reason"), TEXT("not_activatable_widget"));
    }
    else
    {
        for (const TPair<const TCHAR*, const TCHAR*>& Field : {
            TPair<const TCHAR*, const TCHAR*>(TEXT("is_back_handler"), TEXT("bIsBackHandler")),
            {TEXT("display_back_action"), TEXT("bIsBackActionDisplayedInActionBar")},
            {TEXT("auto_activate"), TEXT("bAutoActivate")},
            {TEXT("supports_activation_focus"), TEXT("bSupportsActivationFocus")},
            {TEXT("is_modal"), TEXT("bIsModal")},
            {TEXT("auto_restore_focus"), TEXT("bAutoRestoreFocus")},
            {TEXT("override_action_domain"), TEXT("bOverrideActionDomain")},
            {TEXT("set_visibility_on_activated"), TEXT("bSetVisibilityOnActivated")},
            {TEXT("set_visibility_on_deactivated"), TEXT("bSetVisibilityOnDeactivated")}})
        {
            if (!AddBoolean(Widget, Activation, Field.Key, Field.Value, OutError))
            {
                return false;
            }
        }
        TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
        if (!ExactText(Widget, TEXT("OverrideBackActionDisplayName"), Value))
        {
            SetError(OutError, TEXT("extension_contract_violation"),
                TEXT("The CommonUI back-action display name property is unavailable"));
            return false;
        }
        Activation->SetObjectField(TEXT("back_action_display_name"), Value);
        if (!ExactInteger(Widget, TEXT("InputMappingPriority"), Value))
        {
            SetError(OutError, TEXT("extension_contract_violation"),
                TEXT("The CommonUI input mapping priority property is unavailable"));
            return false;
        }
        Activation->SetObjectField(TEXT("input_mapping_priority"), Value);
        if (!ExactExported(Widget, TEXT("ActivatedVisibility"), Value))
        {
            SetError(OutError, TEXT("extension_contract_violation"),
                TEXT("The CommonUI activated visibility property is unavailable"));
            return false;
        }
        Activation->SetObjectField(TEXT("activated_visibility"), Value);
        if (!ExactExported(Widget, TEXT("DeactivatedVisibility"), Value))
        {
            SetError(OutError, TEXT("extension_contract_violation"),
                TEXT("The CommonUI deactivated visibility property is unavailable"));
            return false;
        }
        Activation->SetObjectField(TEXT("deactivated_visibility"), Value);

        if (!HardReference(Widget, TEXT("InputMapping"), Value))
        {
            SetError(OutError, TEXT("extension_contract_violation"),
                TEXT("The CommonUI input mapping reference is unavailable"));
            return false;
        }
        References->SetObjectField(TEXT("input_mapping"), Value);
        if (!SoftReference(Widget, TEXT("ActionDomainOverride"), Value))
        {
            SetError(OutError, TEXT("extension_contract_violation"),
                TEXT("The CommonUI action domain reference is unavailable"));
            return false;
        }
        References->SetObjectField(TEXT("action_domain_override"), Value);
    }
    OutRecords.Add(MakeShared<FJsonValueObject>(Activation));
    OutRecords.Add(MakeShared<FJsonValueObject>(References));

    TArray<FString> Fingerprint;
    for (const TCHAR* PropertyName : {
        TEXT("bDisplayInActionBar"), TEXT("bConsumePointerInput"),
        TEXT("bIsBackHandler"), TEXT("bIsBackActionDisplayedInActionBar"),
        TEXT("OverrideBackActionDisplayName"), TEXT("bAutoActivate"),
        TEXT("bSupportsActivationFocus"), TEXT("bIsModal"), TEXT("bAutoRestoreFocus"),
        TEXT("bOverrideActionDomain"), TEXT("InputMapping"), TEXT("InputMappingPriority"),
        TEXT("ActionDomainOverride"), TEXT("bSetVisibilityOnActivated"),
        TEXT("ActivatedVisibility"), TEXT("bSetVisibilityOnDeactivated"),
        TEXT("DeactivatedVisibility")})
    {
        const FProperty* Property = ExactProperty(Widget, PropertyName);
        if (Property == nullptr)
        {
            if (Activatable == nullptr)
            {
                continue;
            }
            SetError(OutError, TEXT("extension_contract_violation"),
                TEXT("A required CommonUI fingerprint property is unavailable"));
            return false;
        }
        FString Encoded;
        Property->ExportText_InContainer(
            0, Encoded, &Widget, ParentDefaults(Widget), const_cast<UCommonUserWidget*>(&Widget), PPF_None);
        Fingerprint.Add(FString(PropertyName) + TEXT("|") + Encoded);
    }
    OutFingerprint = FString::Join(Fingerprint, TEXT("\n"));
    return true;
}

bool SectionRequested(
    const TSharedPtr<FJsonObject>& Arguments,
    const FString& Section)
{
    const TArray<TSharedPtr<FJsonValue>>* Sections = nullptr;
    if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("sections"), Sections)
        || Sections == nullptr)
    {
        return true;
    }
    return Sections->ContainsByPredicate([&Section](const TSharedPtr<FJsonValue>& Value)
    {
        FString Requested;
        return Value.IsValid() && Value->TryGetString(Requested) && Requested == Section;
    });
}

class FCommonUIWidgetInspectionHandler final : public IUnrealMCPExtensionHandler
{
public:
    bool IsReady(FString& OutUnavailableReason) const override
    {
        OutUnavailableReason.Reset();
        return UCommonUserWidget::StaticClass() != nullptr
            && UCommonActivatableWidget::StaticClass() != nullptr;
    }

    bool SupportsTarget(const UObject& Target) const override
    {
        return Target.IsA<UCommonUserWidget>();
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
                TEXT("CommonUI Widget inspection requires the exact inspect operation"));
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
        const UCommonUserWidget* Widget = Cast<UCommonUserWidget>(&Target);
        TArray<TSharedPtr<FJsonValue>> Records;
        FString Fingerprint;
        if (Widget == nullptr || !BuildPayload(*Widget, Records, Fingerprint, OutError))
        {
            if (Widget == nullptr)
            {
                SetError(OutError, TEXT("invalid_asset"),
                    TEXT("The target is not a CommonUI Widget class default object"));
            }
            return false;
        }
        Records.RemoveAll([&Arguments](const TSharedPtr<FJsonValue>& Value)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FString Section;
            return !Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr
                || !Object->IsValid() || !(*Object)->TryGetStringField(TEXT("section"), Section)
                || !SectionRequested(Arguments, Section);
        });

        const TSharedRef<FJsonObject> Capabilities = MakeShared<FJsonObject>();
        Capabilities->SetBoolField(TEXT("inspection"), true);
        Capabilities->SetBoolField(TEXT("mutation"), false);
        Capabilities->SetNumberField(
            TEXT("max_records"), UnrealMCPCommonUI::MaxInspectionRecords);
        Capabilities->SetNumberField(
            TEXT("max_properties"), UnrealMCPCommonUI::MaxInspectedProperties);
        Capabilities->SetArrayField(TEXT("typed_sections"), {
            MakeShared<FJsonValueString>(WidgetSection),
            MakeShared<FJsonValueString>(ActivationSection),
            MakeShared<FJsonValueString>(ReferenceSection)});
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
        const UCommonUserWidget* Widget = Cast<UCommonUserWidget>(&Target);
        TArray<TSharedPtr<FJsonValue>> Records;
        return Widget != nullptr && BuildPayload(*Widget, Records, OutFingerprint, OutError);
    }

    bool ApplyMutation(
        UObject&, const FString&, const TSharedPtr<FJsonObject>&,
        TSharedPtr<FJsonObject>&, FUnrealMCPExtensionError& OutError) override
    {
        SetError(OutError, TEXT("extension_unavailable"),
            TEXT("The CommonUI companion is inspection-only"));
        return false;
    }

    bool ReadBack(
        const UObject&, const FString&, const TSharedPtr<FJsonObject>&,
        TSharedPtr<FJsonObject>&, FUnrealMCPExtensionError& OutError) const override
    {
        SetError(OutError, TEXT("extension_unavailable"),
            TEXT("The CommonUI companion exposes no mutation read-back"));
        return false;
    }
};

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPCommonUIWidgetInspectionTest,
    "UnrealMCP.CommonUI.WidgetBlueprintInspection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPCommonUIWidgetInspectionTest::RunTest(const FString& Parameters)
{
    UPackage* Package = CreatePackage(TEXT("/Engine/Transient/UnrealMCPCommonUIInspectionTest"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        UCommonActivatableWidget::StaticClass(), Package, TEXT("WBP_CommonUIInspectionTest"),
        BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass());
    TestNotNull(TEXT("CommonUI Widget Blueprint is created"), Blueprint);
    if (Blueprint == nullptr)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    UCommonUserWidget* Defaults = Blueprint->GeneratedClass != nullptr
        ? Cast<UCommonUserWidget>(Blueprint->GeneratedClass->GetDefaultObject(false)) : nullptr;
    TestNotNull(TEXT("CommonUI Widget defaults resolve"), Defaults);
    if (Defaults == nullptr)
    {
        return false;
    }
    FBoolProperty* AutoActivate = FindFProperty<FBoolProperty>(
        Defaults->GetClass(), TEXT("bAutoActivate"));
    TestNotNull(TEXT("Exact CommonUI activation property resolves"), AutoActivate);
    if (AutoActivate != nullptr)
    {
        AutoActivate->SetPropertyValue_InContainer(Defaults, true);
    }
    FSoftObjectProperty* ActionDomain = FindFProperty<FSoftObjectProperty>(
        Defaults->GetClass(), TEXT("ActionDomainOverride"));
    TestNotNull(TEXT("Exact CommonUI soft reference resolves"), ActionDomain);
    if (ActionDomain != nullptr)
    {
        FSoftObjectPtr* Value = ActionDomain->ContainerPtrToValuePtr<FSoftObjectPtr>(Defaults);
        *Value = FSoftObjectPtr(FSoftObjectPath(TEXT("/Game/UI/DA_MissingDomain.DA_MissingDomain")));
    }

    const bool bDirtyBefore = Package->IsDirty();
    TArray<TSharedPtr<FJsonValue>> FirstRecords;
    FString FirstFingerprint;
    FUnrealMCPExtensionError Error;
    TestTrue(TEXT("Typed CommonUI payload builds"),
        BuildPayload(*Defaults, FirstRecords, FirstFingerprint, Error));
    TArray<TSharedPtr<FJsonValue>> SecondRecords;
    FString SecondFingerprint;
    TestTrue(TEXT("Repeated CommonUI inspection succeeds"),
        BuildPayload(*Defaults, SecondRecords, SecondFingerprint, Error));
    TestEqual(TEXT("All typed CommonUI sections are returned"),
        FirstRecords.Num(), UnrealMCPCommonUI::MaxInspectionRecords);
    TestEqual(TEXT("CommonUI fingerprint is deterministic"),
        FirstFingerprint, SecondFingerprint);
    TestEqual(TEXT("CommonUI inspection preserves package dirtiness"),
        Package->IsDirty(), bDirtyBefore);
    TestTrue(TEXT("CommonUI fingerprint includes typed state"), !FirstFingerprint.IsEmpty());

    const UCommonUserWidget* BaseDefaults = Cast<UCommonUserWidget>(
        UCommonUserWidget::StaticClass()->GetDefaultObject(false));
    TestNotNull(TEXT("non-activatable CommonUI Widget defaults resolve"), BaseDefaults);
    if (BaseDefaults != nullptr)
    {
        TArray<TSharedPtr<FJsonValue>> BaseRecords;
        FString BaseFingerprint;
        TestTrue(TEXT("non-activatable CommonUI payload builds"),
            BuildPayload(*BaseDefaults, BaseRecords, BaseFingerprint, Error));
        const TSharedPtr<FJsonObject>* ActivationRecord = nullptr;
        for (const TSharedPtr<FJsonValue>& Record : BaseRecords)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FString Section;
            if (Record.IsValid() && Record->TryGetObject(Object) && Object != nullptr
                && (*Object)->TryGetStringField(TEXT("section"), Section)
                && Section == ActivationSection)
            {
                ActivationRecord = Object;
                break;
            }
        }
        bool bSupported = true;
        FString Reason;
        TestTrue(TEXT("non-activatable CommonUI state is explicit"),
            ActivationRecord != nullptr
                && (*ActivationRecord)->TryGetBoolField(TEXT("supported"), bSupported)
                && !bSupported
                && (*ActivationRecord)->TryGetStringField(TEXT("unavailable_reason"), Reason)
                && Reason == TEXT("not_activatable_widget"));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPCommonUIWidgetLiveFixtureTest,
    "UnrealMCP.CommonUI.WidgetBlueprintLiveFixture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPCommonUIWidgetLiveFixtureTest::RunTest(const FString& Parameters)
{
    constexpr TCHAR PackageName[] = TEXT("/Game/UnrealMCPCommonUI/WBP_InspectionFixture");
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());
    TestTrue(TEXT("existing CommonUI fixture is removed"),
        !IFileManager::Get().FileExists(*Filename)
            || IFileManager::Get().Delete(*Filename, false, true));

    UPackage* Package = CreatePackage(PackageName);
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        UCommonActivatableWidget::StaticClass(), Package, TEXT("WBP_InspectionFixture"),
        BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass());
    TestNotNull(TEXT("saved CommonUI Widget fixture is created"), Blueprint);
    if (Blueprint == nullptr)
    {
        return false;
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    UCommonUserWidget* Defaults = Blueprint->GeneratedClass != nullptr
        ? Cast<UCommonUserWidget>(Blueprint->GeneratedClass->GetDefaultObject(false)) : nullptr;
    TestNotNull(TEXT("saved CommonUI Widget defaults resolve"), Defaults);
    if (Defaults == nullptr)
    {
        return false;
    }
    FBoolProperty* AutoActivate = FindFProperty<FBoolProperty>(
        Defaults->GetClass(), TEXT("bAutoActivate"));
    FBoolProperty* BackHandler = FindFProperty<FBoolProperty>(
        Defaults->GetClass(), TEXT("bIsBackHandler"));
    TestNotNull(TEXT("saved CommonUI auto-activation property resolves"), AutoActivate);
    TestNotNull(TEXT("saved CommonUI back-handler property resolves"), BackHandler);
    if (AutoActivate == nullptr || BackHandler == nullptr)
    {
        return false;
    }
    AutoActivate->SetPropertyValue_InContainer(Defaults, true);
    BackHandler->SetPropertyValue_InContainer(Defaults, true);
    FSoftObjectProperty* ActionDomain = FindFProperty<FSoftObjectProperty>(
        Defaults->GetClass(), TEXT("ActionDomainOverride"));
    TestNotNull(TEXT("saved CommonUI action-domain property resolves"), ActionDomain);
    if (ActionDomain == nullptr)
    {
        return false;
    }
    FSoftObjectPtr* Value = ActionDomain->ContainerPtrToValuePtr<FSoftObjectPtr>(Defaults);
    *Value = FSoftObjectPtr(FSoftObjectPath(
        TEXT("/Game/UnrealMCPCommonUI/DA_UnresolvedDomain.DA_UnresolvedDomain")));

    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    SaveArgs.bSlowTask = false;
    TestTrue(TEXT("saved CommonUI Widget fixture persists"),
        UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs));
    UE_LOG(LogTemp, Display, TEXT("UNREAL_MCP_COMMONUI_FIXTURE=%s"), *Blueprint->GetPathName());
    return true;
}
#endif

class FUnrealMCPCommonUIModule final : public IModuleInterface
{
public:
    void StartupModule() override
    {
        FUnrealMCPExtensionContribution Contribution;
        Contribution.ContributionId = TEXT("commonui_widget_inspection");
        Contribution.Category = EUnrealMCPExtensionCategory::AssetFamily;
        Contribution.Access = EUnrealMCPExtensionAccess::Read;
        Contribution.Persistence = EUnrealMCPExtensionPersistence::None;
        Contribution.ToolFamily = TEXT("blueprint_inspect");
        Contribution.Operation = OperationName;
        Contribution.TargetFamily = TEXT("commonui_widget");
        Contribution.TargetClassPath = UCommonUserWidget::StaticClass()->GetPathName();
        Contribution.bAllowDerivedTargetClasses = true;
        Contribution.RequiredLiveCapability = TEXT("commonui_widget_blueprints_inspection");
        Contribution.AllowedArgumentFields = {
            TEXT("mode"), TEXT("sections"), TEXT("graph_id"), TEXT("component_id"),
            TEXT("member_id"), TEXT("function_id"), TEXT("local_id"), TEXT("macro_id"),
            TEXT("custom_event_id"), TEXT("widget_id"), TEXT("property_names"),
            TEXT("include_inherited"), TEXT("page_size")};
        Contribution.StableLimits = {
            {TEXT("records"), UnrealMCPCommonUI::MaxInspectionRecords},
            {TEXT("properties"), UnrealMCPCommonUI::MaxInspectedProperties}};
        Contribution.Handler = MakeShared<FCommonUIWidgetInspectionHandler>();

        FUnrealMCPCompanionRegistration Registration;
        Registration.PluginName = TEXT("UnrealMCPCommonUI");
        Registration.ExtensionId = TEXT("unreal-mcp-commonui");
        Registration.OwningModule = TEXT("UnrealMCPCommonUI");
        Registration.SemanticVersion = UnrealMCPCommonUI::Version;
        Registration.CompanionApiVersion = UnrealMCPCommonUI::CompanionApiVersion;
        Registration.ExtensionSchemaRevision = UnrealMCPCommonUI::ExtensionSchemaRevision;
        Registration.RequiredEnginePlugins = {TEXT("CommonUI")};
        Registration.RequiredEngineModules = {TEXT("CommonInput"), TEXT("CommonUI")};
        Registration.Contributions.Add(MoveTemp(Contribution));
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

IMPLEMENT_MODULE(FUnrealMCPCommonUIModule, UnrealMCPCommonUI)
