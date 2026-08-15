#include "IUnrealMCPModule.h"
#include "UnrealMCPCompanionApi.h"
#include "UnrealMCPCommonUIWidgetTreeInspection.h"
#include "UnrealMCPCommonUIVersion.h"

#include "CommonActivatableWidget.h"
#include "CommonTextBlock.h"
#include "CommonUserWidget.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "UnrealMCPWireTypes.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "UObject/UnrealType.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
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
    OutError.Details = MakeShared<FUnrealMCPRecord>();
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

TSharedRef<FUnrealMCPRecord> Boolean(bool Value, const FString& Source)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetBoolField(TEXT("value"), Value);
    Result->SetStringField(TEXT("source"), Source);
    return Result;
}

TSharedRef<FUnrealMCPRecord> Scalar(const FString& Value, const FString& Source)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("value"), Value);
    Result->SetStringField(TEXT("source"), Source);
    return Result;
}

TSharedRef<FUnrealMCPRecord> Integer(int32 Value, const FString& Source)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetNumberField(TEXT("value"), Value);
    Result->SetStringField(TEXT("source"), Source);
    return Result;
}

bool ExactBoolean(
    const UCommonUserWidget& Widget,
    const TCHAR* Name,
    TSharedRef<FUnrealMCPRecord>& OutValue)
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
    TSharedRef<FUnrealMCPRecord>& OutValue)
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
    TSharedRef<FUnrealMCPRecord>& OutValue)
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
    TSharedRef<FUnrealMCPRecord>& OutValue)
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

TSharedRef<FUnrealMCPRecord> EmptyReference(
    const UCommonUserWidget& Widget,
    const FProperty* Property,
    const TCHAR* PropertyName)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
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
    TSharedRef<FUnrealMCPRecord>& OutValue)
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
    TSharedRef<FUnrealMCPRecord>& OutValue)
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
    const TSharedRef<FUnrealMCPRecord>& Record,
    const TCHAR* Field,
    const TCHAR* Property,
    FUnrealMCPExtensionError& OutError)
{
    TSharedRef<FUnrealMCPRecord> Value = MakeShared<FUnrealMCPRecord>();
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
    TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
    FString& OutFingerprint,
    FUnrealMCPExtensionError& OutError)
{
    const TSharedRef<FUnrealMCPRecord> WidgetRecord = MakeShared<FUnrealMCPRecord>();
    WidgetRecord->SetStringField(TEXT("section"), WidgetSection);
    if (!AddBoolean(Widget, WidgetRecord, TEXT("display_in_action_bar"),
            TEXT("bDisplayInActionBar"), OutError)
        || !AddBoolean(Widget, WidgetRecord, TEXT("consume_pointer_input"),
            TEXT("bConsumePointerInput"), OutError))
    {
        return false;
    }
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(WidgetRecord));

    const UCommonActivatableWidget* Activatable = Cast<UCommonActivatableWidget>(&Widget);
    const TSharedRef<FUnrealMCPRecord> Activation = MakeShared<FUnrealMCPRecord>();
    Activation->SetStringField(TEXT("section"), ActivationSection);
    Activation->SetBoolField(TEXT("supported"), Activatable != nullptr);
    const TSharedRef<FUnrealMCPRecord> References = MakeShared<FUnrealMCPRecord>();
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
        TSharedRef<FUnrealMCPRecord> Value = MakeShared<FUnrealMCPRecord>();
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
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(Activation));
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(References));

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
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    const FString& Section)
{
    const TArray<TSharedPtr<FUnrealMCPValue>>* Sections = nullptr;
    if (!Arguments.IsValid() || !Arguments->TryGetArrayField(TEXT("sections"), Sections)
        || Sections == nullptr)
    {
        return true;
    }
    return Sections->ContainsByPredicate([&Section](const TSharedPtr<FUnrealMCPValue>& Value)
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
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
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
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPExtensionError& OutError) override
    {
        const UCommonUserWidget* Widget = Cast<UCommonUserWidget>(&Target);
        TArray<TSharedPtr<FUnrealMCPValue>> Records;
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
        Records.RemoveAll([&Arguments](const TSharedPtr<FUnrealMCPValue>& Value)
        {
            const TSharedPtr<FUnrealMCPRecord>* Object = nullptr;
            FString Section;
            return !Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr
                || !Object->IsValid() || !(*Object)->TryGetStringField(TEXT("section"), Section)
                || !SectionRequested(Arguments, Section);
        });

        const TSharedRef<FUnrealMCPRecord> Capabilities = MakeShared<FUnrealMCPRecord>();
        Capabilities->SetBoolField(TEXT("inspection"), true);
        Capabilities->SetBoolField(TEXT("mutation"), false);
        Capabilities->SetNumberField(
            TEXT("max_records"), UnrealMCPCommonUI::MaxInspectionRecords);
        Capabilities->SetNumberField(
            TEXT("max_properties"), UnrealMCPCommonUI::MaxInspectedProperties);
        Capabilities->SetArrayField(TEXT("typed_sections"), {
            MakeShared<FUnrealMCPValueString>(WidgetSection),
            MakeShared<FUnrealMCPValueString>(ActivationSection),
            MakeShared<FUnrealMCPValueString>(ReferenceSection)});
        OutResult = MakeShared<FUnrealMCPRecord>();
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
        TArray<TSharedPtr<FUnrealMCPValue>> Records;
        return Widget != nullptr && BuildPayload(*Widget, Records, OutFingerprint, OutError);
    }

    bool ApplyMutation(
        UObject&, const FString&, const TSharedPtr<FUnrealMCPRecord>&,
        TSharedPtr<FUnrealMCPRecord>&, FUnrealMCPExtensionError& OutError) override
    {
        SetError(OutError, TEXT("extension_unavailable"),
            TEXT("The CommonUI companion is inspection-only"));
        return false;
    }

    bool ReadBack(
        const UObject&, const FString&, const TSharedPtr<FUnrealMCPRecord>&,
        TSharedPtr<FUnrealMCPRecord>&, FUnrealMCPExtensionError& OutError) const override
    {
        SetError(OutError, TEXT("extension_unavailable"),
            TEXT("The CommonUI companion exposes no mutation read-back"));
        return false;
    }
};

UCommonUserWidget* ResolveCommonUIWidgetDefaults(UObject* Asset)
{
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
    UClass* GeneratedClass = Blueprint != nullptr
        ? (Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass : Blueprint->ParentClass)
        : nullptr;
    return GeneratedClass != nullptr
        ? Cast<UCommonUserWidget>(GeneratedClass->GetDefaultObject(false)) : nullptr;
}

UUserWidget* ResolveWidgetDefaults(UObject* Asset)
{
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
    UClass* GeneratedClass = Blueprint != nullptr
        ? (Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass : Blueprint->ParentClass)
        : nullptr;
    return GeneratedClass != nullptr
        ? Cast<UUserWidget>(GeneratedClass->GetDefaultObject(false)) : nullptr;
}

void ConvertCommonUIInspectionError(
    const FUnrealMCPExtensionError& Input,
    FUnrealMCPError& Output)
{
    Output.Code = Input.Code;
    Output.Message = Input.Message;
    Output.Details = Input.Details;
    Output.bRetryable = Input.bRetryable;
}

class FCommonUIWidgetAssetInspectionAdapter final
    : public IUnrealMCPAssetFamilyInspectionAdapter
{
public:
    bool Inspect(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPAssetFamilySelectorRouter& Selectors,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        if (Context.bHasPartialGraphFlag)
        {
            OutError = {TEXT("invalid_argument"),
                TEXT("CommonUI selectors do not support graph flags")};
            return false;
        }
        const TArray<FString> Sections = {
            WidgetSection, ActivationSection, ReferenceSection,
            UnrealMCP::CommonUIWidgetTreeInspection::Section};
        for (const FString& Section : Sections)
        {
            const bool bWidgetCollection =
                Section == UnrealMCP::CommonUIWidgetTreeInspection::Section;
            if (!Selectors.Register({Section, {Section}, bWidgetCollection, false}, OutError))
            {
                return false;
            }
        }
        if (!Selectors.Freeze(OutError))
        {
            return false;
        }
        const FUnrealMCPAssetFamilySelectorRoute* SelectedRoute = nullptr;
        if (!Context.Selector.IsRoot())
        {
            SelectedRoute = Selectors.Resolve(Context.Selector, OutError);
            if (SelectedRoute == nullptr)
            {
                return false;
            }
        }
        UUserWidget* Defaults = ResolveWidgetDefaults(Context.Asset);
        if (Defaults == nullptr)
        {
            OutError = {TEXT("invalid_asset"),
                TEXT("The asset does not represent a Widget Blueprint class")};
            return false;
        }
        UCommonUserWidget* Widget = Cast<UCommonUserWidget>(Defaults);
        const bool bTreeRoute = SelectedRoute == nullptr
            || SelectedRoute->Identity == UnrealMCP::CommonUIWidgetTreeInspection::Section;
        if (bTreeRoute
            && !UnrealMCP::CommonUIWidgetTreeInspection::Inspect(
                Context, Document, Snapshot, OutError))
        {
            return false;
        }
        if (SelectedRoute != nullptr
            && SelectedRoute->Identity == UnrealMCP::CommonUIWidgetTreeInspection::Section)
        {
            return true;
        }
        if (Widget == nullptr)
        {
            if (SelectedRoute != nullptr)
            {
                OutError = {TEXT("unsupported_operation"),
                    TEXT("Root CommonUI selectors require a UCommonUserWidget-derived Widget Blueprint")};
                return false;
            }
            if (Context.Selector.IsRoot())
            {
                TArray<TSharedPtr<FUnrealMCPValue>> Values;
                Values.Add(MakeShared<FUnrealMCPValueString>(
                    UnrealMCP::CommonUIWidgetTreeInspection::Section));
                return Document.Add({TEXT("selectors"), TEXT("array"),
                    MakeShared<FUnrealMCPValueArray>(MoveTemp(Values))}, OutError);
            }
            return true;
        }
        if (Context.bHasPaging)
        {
            OutError = {TEXT("invalid_argument"),
                TEXT("Paging parameters require the commonui_widgets collection selector")};
            return false;
        }
        const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
        Arguments->SetStringField(TEXT("mode"), TEXT("inspect"));
        TArray<TSharedPtr<FUnrealMCPValue>> Requested;
        if (SelectedRoute != nullptr)
        {
            Requested.Add(MakeShared<FUnrealMCPValueString>(SelectedRoute->Identity));
        }
        else
        {
            for (const FString& Section : Sections)
            {
                if (Section != UnrealMCP::CommonUIWidgetTreeInspection::Section)
                    Requested.Add(MakeShared<FUnrealMCPValueString>(Section));
            }
        }
        Arguments->SetArrayField(TEXT("sections"), MoveTemp(Requested));
        TSharedPtr<FUnrealMCPRecord> Result;
        FUnrealMCPExtensionError ExtensionError;
        if (!Handler.Inspect(*Widget, OperationName, Arguments, Result, ExtensionError))
        {
            ConvertCommonUIInspectionError(ExtensionError, OutError);
            return false;
        }
        const TArray<TSharedPtr<FUnrealMCPValue>>* Records = nullptr;
        if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("records"), Records)
            || Records == nullptr)
        {
            OutError = {TEXT("extension_contract_violation"),
                TEXT("The CommonUI adapter returned invalid semantic blocks")};
            return false;
        }
        for (const TSharedPtr<FUnrealMCPValue>& Value : *Records)
        {
            const TSharedPtr<FUnrealMCPRecord>* Block = nullptr;
            FString Section;
            if (!Value.IsValid() || !Value->TryGetObject(Block)
                || Block == nullptr || !Block->IsValid()
                || !(*Block)->TryGetStringField(TEXT("section"), Section)
                || Section == UnrealMCP::CommonUIWidgetTreeInspection::Section
                || !Sections.Contains(Section))
            {
                OutError = {TEXT("extension_contract_violation"),
                    TEXT("The CommonUI adapter returned invalid semantic blocks")};
                return false;
            }
            (*Block)->RemoveField(TEXT("section"));
            if (!Document.Add({Section, TEXT("record"),
                MakeShared<FUnrealMCPValueObject>(*Block)}, OutError))
            {
                return false;
            }
        }
        if (Context.Selector.IsRoot())
        {
            TArray<TSharedPtr<FUnrealMCPValue>> Values;
            for (const FString& Section : Sections)
            {
                Values.Add(MakeShared<FUnrealMCPValueString>(Section));
            }
            if (!Document.Add({TEXT("selectors"), TEXT("array"),
                MakeShared<FUnrealMCPValueArray>(MoveTemp(Values))}, OutError))
            {
                return false;
            }
        }
        else
        {
            const TSharedRef<FUnrealMCPRecord> Selection = MakeShared<FUnrealMCPRecord>();
            Selection->SetStringField(TEXT("selector"), SelectedRoute->Identity);
            Selection->SetStringField(TEXT("kind"), TEXT("record"));
            if (!Document.Add({TEXT("selection"), TEXT("record"),
                MakeShared<FUnrealMCPValueObject>(Selection)}, OutError))
            {
                return false;
            }
        }
        FString Fingerprint;
        if (!Handler.AppendFingerprint(*Widget, OperationName, Fingerprint, ExtensionError))
        {
            ConvertCommonUIInspectionError(ExtensionError, OutError);
            return false;
        }
        return Snapshot.Add(TEXT("commonui_widget"), Fingerprint, OutError);
    }

private:
    FCommonUIWidgetInspectionHandler Handler;
};

FUnrealMCPCompanionAssetFamily CommonUIWidgetFamily()
{
    FUnrealMCPCompanionAssetFamily Family;
    Family.FamilyId = TEXT("commonui_widget");
    Family.NativeClassPath = UUserWidget::StaticClass()->GetPathName();
    Family.ClassPolicy = EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived;
    Family.Priority = 200;
    Family.RequiredModules = {TEXT("CommonUI")};
    Family.Bounds.MaxDocumentBytes = 4 * 1024 * 1024;
    Family.Bounds.MaxValueNodes = 65536;
    Family.Limits = {
        {TEXT("records"), UnrealMCPCommonUI::MaxInspectionRecords},
        {TEXT("properties"), UnrealMCPCommonUI::MaxInspectedProperties},
        {TEXT("commonui_widgets"), UnrealMCPCommonUI::MaxWidgetTreeWidgets},
        {TEXT("properties_per_widget"), UnrealMCPCommonUI::MaxPropertiesPerWidget},
        {TEXT("input_action_references"), UnrealMCPCommonUI::MaxInputActionReferences}};
    Family.Capabilities.bInspection = true;
    Family.SelectorRoutes = {
        {WidgetSection, {WidgetSection}, false, false},
        {ActivationSection, {ActivationSection}, false, false},
        {ReferenceSection, {ReferenceSection}, false, false},
        {UnrealMCP::CommonUIWidgetTreeInspection::Section,
            {UnrealMCP::CommonUIWidgetTreeInspection::Section}, true, false}};
    Family.StableNestedIdentityKinds = {TEXT("commonui_widget")};
    Family.InspectionAdapter = MakeShared<FCommonUIWidgetAssetInspectionAdapter>();
    Family.SnapshotBuilder = [](UObject* Asset)
    {
        UCommonUserWidget* Widget = ResolveCommonUIWidgetDefaults(Asset);
        FCommonUIWidgetInspectionHandler Handler;
        FUnrealMCPExtensionError Error;
        FString RootFingerprint = TEXT("not_commonui_root");
        if (Widget != nullptr && !Handler.AppendFingerprint(
                *Widget, OperationName, RootFingerprint, Error))
            return FString();
        const FString TreeFingerprint =
            UnrealMCP::CommonUIWidgetTreeInspection::BuildFingerprint(Asset);
        return TreeFingerprint.IsEmpty()
            ? FString() : RootFingerprint + TEXT("\n") + TreeFingerprint;
    };
    return Family;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPCommonUIWidgetInspectionTest,
    "UnrealMCP.CommonUI.WidgetBlueprintInspection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPCommonUIWidgetInspectionTest::RunTest(const FString& Parameters)
{
    int32 FamilyCount = 0;
    FUnrealMCPError AllowlistError;
    TestTrue(TEXT("Every frozen CommonUI widget family and property resolves in UE 5.8"),
        UnrealMCP::CommonUIWidgetTreeInspection::ValidateFrozenAllowlist(
            FamilyCount, AllowlistError));
    TestEqual(TEXT("The frozen CommonUI widget-family count is stable"), FamilyCount, 21);

    UPackage* Package = CreatePackage(TEXT("/Engine/Transient/UnrealMCPCommonUIInspectionTest"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        UCommonActivatableWidget::StaticClass(), Package, TEXT("WBP_CommonUIInspectionTest"),
        BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass());
    TestNotNull(TEXT("CommonUI Widget Blueprint is created"), Blueprint);
    if (Blueprint == nullptr)
    {
        return false;
    }
    UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
    UCommonTextBlock* FixtureText = WidgetBlueprint != nullptr
        ? WidgetBlueprint->WidgetTree->ConstructWidget<UCommonTextBlock>(
            UCommonTextBlock::StaticClass(), TEXT("FixtureCommonText")) : nullptr;
    TestNotNull(TEXT("saved CommonUI child fixture is created"), FixtureText);
    if (WidgetBlueprint == nullptr || FixtureText == nullptr)
    {
        return false;
    }
    WidgetBlueprint->WidgetTree->RootWidget = FixtureText;
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
    TArray<TSharedPtr<FUnrealMCPValue>> FirstRecords;
    FString FirstFingerprint;
    FUnrealMCPExtensionError Error;
    TestTrue(TEXT("Typed CommonUI payload builds"),
        BuildPayload(*Defaults, FirstRecords, FirstFingerprint, Error));
    TArray<TSharedPtr<FUnrealMCPValue>> SecondRecords;
    FString SecondFingerprint;
    TestTrue(TEXT("Repeated CommonUI inspection succeeds"),
        BuildPayload(*Defaults, SecondRecords, SecondFingerprint, Error));
    TestEqual(TEXT("All typed CommonUI sections are returned"),
        FirstRecords.Num(), UnrealMCPCommonUI::MaxRootInspectionRecords);
    TestEqual(TEXT("CommonUI fingerprint is deterministic"),
        FirstFingerprint, SecondFingerprint);
    TestEqual(TEXT("CommonUI inspection preserves package dirtiness"),
        Package->IsDirty(), bDirtyBefore);
    TestTrue(TEXT("CommonUI fingerprint includes typed state"), !FirstFingerprint.IsEmpty());

    FCommonUIWidgetAssetInspectionAdapter Adapter;
    FUnrealMCPAssetFamilyInspectionContext Context;
    Context.Asset = Blueprint;
    Context.Identity = {Blueprint->GetPathName(), TEXT("fixture_snapshot")};
    FUnrealMCPAssetFamilyDocumentBuilder Document{FUnrealMCPAssetFamilyLimits()};
    FUnrealMCPAssetFamilySelectorRouter Selectors{FUnrealMCPAssetFamilyLimits()};
    FUnrealMCPAssetFamilySnapshotBuilder Snapshot{FUnrealMCPAssetFamilyLimits()};
    FUnrealMCPError AdapterError;
    TestTrue(TEXT("CommonUI common asset adapter returns its semantic blocks"),
        Adapter.Inspect(Context, Document, Selectors, Snapshot, AdapterError));
    for (const FString& Section : {
        FString(WidgetSection), FString(ActivationSection), FString(ReferenceSection)})
    {
        TestTrue(FString::Printf(TEXT("CommonUI adapter publishes %s"), *Section),
            Document.GetRecords().ContainsByPredicate(
                [&Section](const FUnrealMCPAssetFamilyValueRecord& Value)
                { return Value.Path == Section; }));
    }

    const UCommonUserWidget* BaseDefaults = Cast<UCommonUserWidget>(
        UCommonUserWidget::StaticClass()->GetDefaultObject(false));
    TestNotNull(TEXT("non-activatable CommonUI Widget defaults resolve"), BaseDefaults);
    if (BaseDefaults != nullptr)
    {
        TArray<TSharedPtr<FUnrealMCPValue>> BaseRecords;
        FString BaseFingerprint;
        TestTrue(TEXT("non-activatable CommonUI payload builds"),
            BuildPayload(*BaseDefaults, BaseRecords, BaseFingerprint, Error));
        const TSharedPtr<FUnrealMCPRecord>* ActivationRecord = nullptr;
        for (const TSharedPtr<FUnrealMCPValue>& Record : BaseRecords)
        {
            const TSharedPtr<FUnrealMCPRecord>* Object = nullptr;
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

    UPackage* OrdinaryPackage = CreatePackage(
        TEXT("/Engine/Transient/UnrealMCPCommonUIOrdinaryWidgetTest"));
    UWidgetBlueprint* OrdinaryBlueprint = Cast<UWidgetBlueprint>(
        FKismetEditorUtilities::CreateBlueprint(
            UUserWidget::StaticClass(), OrdinaryPackage, TEXT("WBP_OrdinaryWithCommonUIText"),
            BPTYPE_Normal, UWidgetBlueprint::StaticClass(),
            UWidgetBlueprintGeneratedClass::StaticClass()));
    TestNotNull(TEXT("ordinary Widget Blueprint is created"), OrdinaryBlueprint);
    UCommonTextBlock* CommonText = OrdinaryBlueprint != nullptr
        ? OrdinaryBlueprint->WidgetTree->ConstructWidget<UCommonTextBlock>(
            UCommonTextBlock::StaticClass(), TEXT("CommonText")) : nullptr;
    TestNotNull(TEXT("CommonUI child is created in the ordinary Widget Blueprint"), CommonText);
    if (OrdinaryBlueprint != nullptr && CommonText != nullptr)
    {
        OrdinaryBlueprint->WidgetTree->RootWidget = CommonText;
        FKismetEditorUtilities::CompileBlueprint(OrdinaryBlueprint);
        const bool bOrdinaryDirtyBefore = OrdinaryPackage->IsDirty();
        FUnrealMCPAssetFamilyInspectionContext OrdinaryContext;
        OrdinaryContext.Asset = OrdinaryBlueprint;
        OrdinaryContext.Identity = {OrdinaryBlueprint->GetPathName(), TEXT("ordinary_snapshot")};
        FUnrealMCPAssetFamilyDocumentBuilder OrdinaryDocument{FUnrealMCPAssetFamilyLimits()};
        FUnrealMCPAssetFamilySelectorRouter OrdinarySelectors{FUnrealMCPAssetFamilyLimits()};
        FUnrealMCPAssetFamilySnapshotBuilder OrdinarySnapshot{FUnrealMCPAssetFamilyLimits()};
        TestTrue(TEXT("ordinary Widget Blueprint receives the CommonUI tree overlay"),
            Adapter.Inspect(OrdinaryContext, OrdinaryDocument, OrdinarySelectors,
                OrdinarySnapshot, AdapterError));
        const FUnrealMCPAssetFamilyValueRecord* Collection =
            OrdinaryDocument.GetRecords().FindByPredicate(
                [](const FUnrealMCPAssetFamilyValueRecord& Value)
                { return Value.Path == UnrealMCP::CommonUIWidgetTreeInspection::Section; });
        const TSharedPtr<FUnrealMCPRecord>* CollectionRecord = nullptr;
        double Count = 0.0;
        TestTrue(TEXT("ordinary root advertises one CommonUI child"),
            Collection != nullptr && Collection->Value.IsValid()
                && Collection->Value->TryGetObject(CollectionRecord)
                && CollectionRecord != nullptr && CollectionRecord->IsValid()
                && (*CollectionRecord)->TryGetNumberField(TEXT("count"), Count)
                && Count == 1.0);

        FUnrealMCPAssetFamilyInspectionContext PageContext = OrdinaryContext;
        PageContext.Selector.Segments = {UnrealMCP::CommonUIWidgetTreeInspection::Section};
        PageContext.PageSize = 1;
        PageContext.bHasPaging = true;
        FUnrealMCPAssetFamilyDocumentBuilder PageDocument{FUnrealMCPAssetFamilyLimits()};
        FUnrealMCPAssetFamilySelectorRouter PageSelectors{FUnrealMCPAssetFamilyLimits()};
        FUnrealMCPAssetFamilySnapshotBuilder PageSnapshot{FUnrealMCPAssetFamilyLimits()};
        TestTrue(TEXT("CommonUI widget collection supports bounded paging"),
            Adapter.Inspect(PageContext, PageDocument, PageSelectors, PageSnapshot, AdapterError));
        const FUnrealMCPAssetFamilyValueRecord* Page = PageDocument.GetRecords().FindByPredicate(
            [](const FUnrealMCPAssetFamilyValueRecord& Value)
            { return Value.Path == UnrealMCP::CommonUIWidgetTreeInspection::Section; });
        const TArray<TSharedPtr<FUnrealMCPValue>>* PageValues = nullptr;
        TestTrue(TEXT("CommonUI widget page returns one typed child"),
            Page != nullptr && Page->Value.IsValid() && Page->Value->TryGetArray(PageValues)
                && PageValues != nullptr && PageValues->Num() == 1);
        TestEqual(TEXT("CommonUI child inspection preserves package dirtiness"),
            OrdinaryPackage->IsDirty(), bOrdinaryDirtyBefore);
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
    UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);
    UCommonTextBlock* FixtureText = WidgetBlueprint != nullptr
        ? WidgetBlueprint->WidgetTree->ConstructWidget<UCommonTextBlock>(
            UCommonTextBlock::StaticClass(), TEXT("FixtureCommonText")) : nullptr;
    TestNotNull(TEXT("saved CommonUI child fixture is created"), FixtureText);
    if (WidgetBlueprint == nullptr || FixtureText == nullptr)
    {
        return false;
    }
    WidgetBlueprint->WidgetTree->RootWidget = FixtureText;
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
        FUnrealMCPCompanionRegistration Registration;
        Registration.PluginName = TEXT("UnrealMCPCommonUI");
        Registration.ExtensionId = TEXT("unreal-mcp-commonui");
        Registration.OwningModule = TEXT("UnrealMCPCommonUI");
        Registration.SemanticVersion = UnrealMCPCommonUI::Version;
        Registration.CompanionApiVersion = UnrealMCPCommonUI::CompanionApiVersion;
        Registration.ExtensionSchemaRevision = UnrealMCPCommonUI::ExtensionSchemaRevision;
        Registration.RequiredEnginePlugins = {TEXT("CommonUI")};
        Registration.RequiredEngineModules = {TEXT("CommonInput"), TEXT("CommonUI")};
        Registration.AssetFamilies.Add(CommonUIWidgetFamily());
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
