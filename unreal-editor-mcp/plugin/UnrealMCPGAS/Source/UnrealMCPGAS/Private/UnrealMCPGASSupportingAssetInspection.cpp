#include "IUnrealMCPModule.h"
#include "UnrealMCPCompanionApi.h"
#include "UnrealMCPGASVersion.h"

#include "AttributeSet.h"
#include "Engine/Blueprint.h"
#include "GameplayCueNotify_Actor.h"
#include "GameplayCueNotify_Burst.h"
#include "GameplayCueNotify_BurstLatent.h"
#include "GameplayCueNotify_HitImpact.h"
#include "GameplayCueNotify_Looping.h"
#include "GameplayCueNotify_Static.h"
#include "GameplayEffectAttributeCaptureDefinition.h"
#include "GameplayEffectExecutionCalculation.h"
#include "GameplayModMagnitudeCalculation.h"
#include "GameplayTagContainer.h"
#include "Misc/SecureHash.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "EdGraphSchema_K2.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

namespace
{
constexpr TCHAR CueSection[] = TEXT("gameplay_cue_notify");
constexpr TCHAR AttributeSection[] = TEXT("attribute_set");
constexpr TCHAR MagnitudeSection[] = TEXT("gameplay_mod_magnitude_calculation");
constexpr TCHAR ExecutionSection[] = TEXT("gameplay_effect_execution_calculation");

enum class ESupportingFamily : uint8
{
    CueStatic,
    CueActor,
    AttributeSet,
    MagnitudeCalculation,
    ExecutionCalculation,
};

struct FCollectedReference
{
    FString PropertyPath;
    FString ObjectPath;
    FString ClassPath;

    FString Key() const
    {
        return PropertyPath + TEXT("|") + ObjectPath + TEXT("|") + ClassPath;
    }
};

FString StableSupportingIdentity(const FString& Seed)
{
    return FMD5::HashAnsiString(*Seed).ToLower();
}

void SetError(FUnrealMCPError& OutError, const TCHAR* Code, const TCHAR* Message)
{
    OutError = {Code, Message};
}

UObject* ResolveBlueprintDefaults(UObject* Asset, const UClass* RequiredBase)
{
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
    UClass* GeneratedClass = Blueprint != nullptr
        ? (Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass : Blueprint->ParentClass)
        : nullptr;
    if (GeneratedClass == nullptr || RequiredBase == nullptr
        || !GeneratedClass->IsChildOf(RequiredBase))
    {
        return nullptr;
    }
    return GeneratedClass->GetDefaultObject(false);
}

const UObject* ParentDefaults(const UObject& Defaults, const UClass* RequiredBase)
{
    const UClass* ParentClass = Defaults.GetClass()->GetSuperClass();
    return ParentClass != nullptr && ParentClass->IsChildOf(RequiredBase)
        ? ParentClass->GetDefaultObject(false) : nullptr;
}

FString PropertySource(
    const UObject& Defaults,
    const FProperty* Property,
    const UClass* RequiredBase)
{
    if (Property == nullptr)
    {
        return TEXT("local");
    }
    const UClass* OwnerClass = Property->GetOwner<UClass>();
    if (OwnerClass == Defaults.GetClass())
    {
        return TEXT("local");
    }
    const UObject* Parent = ParentDefaults(Defaults, RequiredBase);
    return Parent != nullptr && OwnerClass != nullptr
        && Parent->GetClass()->IsChildOf(OwnerClass)
        && Property->Identical_InContainer(&Defaults, Parent)
        ? TEXT("inherited") : TEXT("local");
}

FString BlueprintAssetPath(const UClass* Class)
{
    const UBlueprint* Blueprint = Class != nullptr ? Cast<UBlueprint>(Class->ClassGeneratedBy) : nullptr;
    return Blueprint != nullptr ? Blueprint->GetPathName() : FString();
}

bool AddReference(
    const FString& PropertyPath,
    UObject* Object,
    TArray<FCollectedReference>& OutReferences,
    TSet<FString>& Seen,
    FUnrealMCPError& OutError)
{
    if (Object == nullptr)
    {
        return true;
    }
    FCollectedReference Reference;
    Reference.PropertyPath = PropertyPath;
    if (const UClass* Class = Cast<UClass>(Object))
    {
        Reference.ClassPath = Class->GetPathName();
        Reference.ObjectPath = BlueprintAssetPath(Class);
    }
    else
    {
        Reference.ObjectPath = Object->GetPathName();
        Reference.ClassPath = Object->GetClass()->GetPathName();
    }
    const FString Key = Reference.Key();
    if (Seen.Contains(Key))
    {
        return true;
    }
    if (OutReferences.Num() >= UnrealMCPGAS::MaxSupportingReferences)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("Supporting GAS asset references exceed the output bound"));
        return false;
    }
    Seen.Add(Key);
    OutReferences.Add(MoveTemp(Reference));
    return true;
}

bool AddSoftReference(
    const FString& PropertyPath,
    const FSoftObjectPath& ObjectPath,
    const FProperty& Property,
    TArray<FCollectedReference>& OutReferences,
    TSet<FString>& Seen,
    FUnrealMCPError& OutError)
{
    if (!ObjectPath.IsValid())
    {
        return true;
    }
    FCollectedReference Reference;
    Reference.PropertyPath = PropertyPath;
    Reference.ObjectPath = ObjectPath.ToString();
    if (const FSoftClassProperty* SoftClass = CastField<FSoftClassProperty>(&Property))
    {
        Reference.ClassPath = SoftClass->MetaClass != nullptr
            ? SoftClass->MetaClass->GetPathName() : FString();
    }
    const FString Key = Reference.Key();
    if (Seen.Contains(Key))
    {
        return true;
    }
    if (OutReferences.Num() >= UnrealMCPGAS::MaxSupportingReferences)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("Supporting GAS asset references exceed the output bound"));
        return false;
    }
    Seen.Add(Key);
    OutReferences.Add(MoveTemp(Reference));
    return true;
}

bool CollectReferences(
    const FProperty& Property,
    const void* Value,
    const FString& PropertyPath,
    int32 Depth,
    int32& ScanCount,
    TArray<FCollectedReference>& OutReferences,
    TSet<FString>& Seen,
    FUnrealMCPError& OutError)
{
    if (Value == nullptr)
    {
        return true;
    }
    if (Depth > UnrealMCPGAS::MaxSupportingTraversalDepth
        || ++ScanCount > UnrealMCPGAS::MaxSupportingCollectionScan)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("A supporting GAS property exceeds the traversal bound"));
        return false;
    }
    if (const FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(&Property))
    {
        return AddSoftReference(PropertyPath,
            SoftObject->GetPropertyValue(Value).ToSoftObjectPath(), Property,
            OutReferences, Seen, OutError);
    }
    if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(&Property))
    {
        return AddReference(PropertyPath, Object->GetObjectPropertyValue(Value),
            OutReferences, Seen, OutError);
    }
    if (const FStructProperty* Struct = CastField<FStructProperty>(&Property))
    {
        for (TFieldIterator<FProperty> It(Struct->Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
        {
            const FProperty* Child = *It;
            if (!CollectReferences(*Child, Child->ContainerPtrToValuePtr<void>(Value),
                PropertyPath + TEXT(".") + Child->GetName(), Depth + 1, ScanCount,
                OutReferences, Seen, OutError))
            {
                return false;
            }
        }
        return true;
    }
    if (const FArrayProperty* Array = CastField<FArrayProperty>(&Property))
    {
        FScriptArrayHelper Helper(Array, const_cast<void*>(Value));
        if (Helper.Num() + ScanCount > UnrealMCPGAS::MaxSupportingCollectionScan)
        {
            SetError(OutError, TEXT("response_too_large"),
                TEXT("A supporting GAS collection exceeds the scan bound"));
            return false;
        }
        for (int32 Index = 0; Index < Helper.Num(); ++Index)
        {
            if (!CollectReferences(*Array->Inner, Helper.GetRawPtr(Index),
                FString::Printf(TEXT("%s[%d]"), *PropertyPath, Index), Depth + 1,
                ScanCount, OutReferences, Seen, OutError))
            {
                return false;
            }
        }
    }
    return true;
}

bool ExportProperty(
    const UObject& Defaults,
    const FProperty& Property,
    const UClass* RequiredBase,
    const TSharedRef<FUnrealMCPRecord>& OutRecord,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    FString Encoded;
    Property.ExportText_InContainer(0, Encoded, &Defaults,
        ParentDefaults(Defaults, RequiredBase), const_cast<UObject*>(&Defaults), PPF_None);
    if (Encoded.Len() * static_cast<int32>(sizeof(TCHAR))
        > UnrealMCPGAS::MaxSupportingPropertyBytes)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("A supporting GAS persisted property exceeds the encoded bound"));
        return false;
    }
    OutRecord->SetStringField(TEXT("name"), Property.GetName());
    OutRecord->SetStringField(TEXT("type"), Property.GetCPPType());
    OutRecord->SetStringField(TEXT("value"), Encoded);
    OutRecord->SetStringField(TEXT("source"), PropertySource(Defaults, &Property, RequiredBase));
    OutFingerprint += Property.GetName() + TEXT("|") + Encoded + TEXT("\n");
    return true;
}

TSharedRef<FUnrealMCPRecord> EncodeTags(const FGameplayTagContainer& Container)
{
    TArray<FGameplayTag> Tags;
    Container.GetGameplayTagArray(Tags);
    Tags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
    {
        return Left.ToString() < Right.ToString();
    });
    TArray<TSharedPtr<FUnrealMCPValue>> Values;
    for (const FGameplayTag& Tag : Tags)
    {
        Values.Add(MakeShared<FUnrealMCPValueString>(Tag.ToString()));
    }
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetArrayField(TEXT("tags"), Values);
    Result->SetNumberField(TEXT("tag_count"), Tags.Num());
    return Result;
}

FString CueKind(const UObject& Defaults)
{
    if (Defaults.IsA<UGameplayCueNotify_HitImpact>()) return TEXT("hit_impact");
    if (Defaults.IsA<UGameplayCueNotify_Burst>()) return TEXT("burst");
    if (Defaults.IsA<AGameplayCueNotify_BurstLatent>()) return TEXT("burst_latent");
    if (Defaults.IsA<AGameplayCueNotify_Looping>()) return TEXT("looping");
    if (Defaults.IsA<AGameplayCueNotify_Actor>()) return TEXT("actor");
    if (Defaults.IsA<UGameplayCueNotify_Static>()) return TEXT("static");
    return TEXT("unsupported");
}

bool BoolProperty(const UObject& Defaults, const TCHAR* Name, bool& OutValue)
{
    const FBoolProperty* Property = FindFProperty<FBoolProperty>(Defaults.GetClass(), Name);
    if (Property == nullptr)
    {
        return false;
    }
    OutValue = Property->GetPropertyValue_InContainer(&Defaults);
    return true;
}

bool BuildCueBlock(
    UObject& Defaults,
    const UClass* RequiredBase,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    const UGameplayCueNotify_Static* StaticCue = Cast<UGameplayCueNotify_Static>(&Defaults);
    const AGameplayCueNotify_Actor* ActorCue = Cast<AGameplayCueNotify_Actor>(&Defaults);
    if (StaticCue == nullptr && ActorCue == nullptr)
    {
        SetError(OutError, TEXT("invalid_asset"),
            TEXT("The asset does not represent a supported Gameplay Cue Notify class"));
        return false;
    }

    const TSharedRef<FUnrealMCPRecord> Summary = MakeShared<FUnrealMCPRecord>();
    const FString Kind = CueKind(Defaults);
    Summary->SetStringField(TEXT("kind"), Kind);
    Summary->SetBoolField(TEXT("supported"), Kind != TEXT("unsupported"));
    Summary->SetStringField(TEXT("class_path"), Defaults.GetClass()->GetPathName());
    Summary->SetStringField(TEXT("parent_class_path"),
        Defaults.GetClass()->GetSuperClass() != nullptr
            ? Defaults.GetClass()->GetSuperClass()->GetPathName() : FString());

    const FStructProperty* TagProperty = FindFProperty<FStructProperty>(
        Defaults.GetClass(), TEXT("GameplayCueTag"));
    const FGameplayTag* Tag = TagProperty != nullptr
        && TagProperty->Struct == FGameplayTag::StaticStruct()
        ? TagProperty->ContainerPtrToValuePtr<FGameplayTag>(&Defaults) : nullptr;
    const FNameProperty* NameProperty = FindFProperty<FNameProperty>(
        Defaults.GetClass(), TEXT("GameplayCueName"));
    bool bOverride = false;
    if (Tag == nullptr || NameProperty == nullptr
        || !BoolProperty(Defaults, TEXT("IsOverride"), bOverride))
    {
        SetError(OutError, TEXT("extension_contract_violation"),
            TEXT("A required Gameplay Cue Notify property is unavailable"));
        return false;
    }
    Summary->SetStringField(TEXT("gameplay_cue_tag"), Tag->ToString());
    Summary->SetStringField(TEXT("gameplay_cue_name"),
        NameProperty->GetPropertyValue_InContainer(&Defaults).ToString());
    Summary->SetBoolField(TEXT("is_override"), bOverride);
    OutBlock->SetObjectField(TEXT("summary"), Summary);

    const TSharedRef<FUnrealMCPRecord> Events = MakeShared<FUnrealMCPRecord>();
    const auto Handles = [StaticCue, ActorCue](EGameplayCueEvent::Type Event)
    {
        return StaticCue != nullptr ? StaticCue->HandlesEvent(Event) : ActorCue->HandlesEvent(Event);
    };
    Events->SetBoolField(TEXT("on_active"), Handles(EGameplayCueEvent::OnActive));
    Events->SetBoolField(TEXT("while_active"), Handles(EGameplayCueEvent::WhileActive));
    Events->SetBoolField(TEXT("executed"), Handles(EGameplayCueEvent::Executed));
    Events->SetBoolField(TEXT("removed"), Handles(EGameplayCueEvent::Removed));
    OutBlock->SetObjectField(TEXT("event_responses"), Events);

    static const TCHAR* PropertyNames[] = {
        TEXT("GameplayCueTag"), TEXT("GameplayCueName"), TEXT("IsOverride"),
        TEXT("bAutoDestroyOnRemove"), TEXT("AutoDestroyDelay"),
        TEXT("WarnIfTimelineIsStillRunning"), TEXT("WarnIfLatentActionIsStillRunning"),
        TEXT("bAutoAttachToOwner"), TEXT("bUniqueInstancePerInstigator"),
        TEXT("bUniqueInstancePerSourceObject"), TEXT("bAllowMultipleOnActiveEvents"),
        TEXT("bAllowMultipleWhileActiveEvents"), TEXT("NumPreallocatedInstances"),
        TEXT("DefaultSpawnCondition"), TEXT("DefaultPlacementInfo"), TEXT("BurstEffects"),
        TEXT("ApplicationEffects"), TEXT("LoopingEffects"), TEXT("RecurringEffects"),
        TEXT("RemovalEffects"), TEXT("Sound"), TEXT("ParticleSystem")};
    TArray<TSharedPtr<FUnrealMCPValue>> Settings;
    TArray<FCollectedReference> References;
    TSet<FString> SeenReferences;
    int32 ScanCount = 0;
    for (const TCHAR* PropertyName : PropertyNames)
    {
        const FProperty* Property = Defaults.GetClass()->FindPropertyByName(FName(PropertyName));
        if (Property == nullptr)
        {
            continue;
        }
        if (Settings.Num() >= UnrealMCPGAS::MaxSupportingProperties)
        {
            SetError(OutError, TEXT("response_too_large"),
                TEXT("Gameplay Cue Notify properties exceed the output bound"));
            return false;
        }
        const TSharedRef<FUnrealMCPRecord> Setting = MakeShared<FUnrealMCPRecord>();
        if (!ExportProperty(Defaults, *Property, RequiredBase, Setting, OutFingerprint, OutError)
            || !CollectReferences(*Property, Property->ContainerPtrToValuePtr<void>(&Defaults),
                Property->GetName(), 0, ScanCount, References, SeenReferences, OutError))
        {
            return false;
        }
        Settings.Add(MakeShared<FUnrealMCPValueObject>(Setting));
    }
    const TSharedRef<FUnrealMCPRecord> Persisted = MakeShared<FUnrealMCPRecord>();
    Persisted->SetNumberField(TEXT("property_count"), Settings.Num());
    Persisted->SetArrayField(TEXT("properties"), Settings);
    OutBlock->SetObjectField(TEXT("persisted_settings"), Persisted);

    References.Sort([](const FCollectedReference& Left, const FCollectedReference& Right)
    {
        return Left.Key() < Right.Key();
    });
    TArray<TSharedPtr<FUnrealMCPValue>> ReferenceValues;
    for (int32 Index = 0; Index < References.Num(); ++Index)
    {
        const FCollectedReference& Reference = References[Index];
        const TSharedRef<FUnrealMCPRecord> Value = MakeShared<FUnrealMCPRecord>();
        Value->SetStringField(TEXT("reference_id"), StableSupportingIdentity(
            Reference.Key() + TEXT("|") + FString::FromInt(Index)));
        Value->SetStringField(TEXT("property_path"), Reference.PropertyPath);
        Value->SetStringField(TEXT("object_path"), Reference.ObjectPath);
        Value->SetStringField(TEXT("class_path"), Reference.ClassPath);
        Value->SetBoolField(TEXT("resolved"), !Reference.ObjectPath.IsEmpty());
        ReferenceValues.Add(MakeShared<FUnrealMCPValueObject>(Value));
        OutFingerprint += Reference.Key() + TEXT("\n");
    }
    const TSharedRef<FUnrealMCPRecord> ReferenceBlock = MakeShared<FUnrealMCPRecord>();
    ReferenceBlock->SetNumberField(TEXT("reference_count"), ReferenceValues.Num());
    ReferenceBlock->SetArrayField(TEXT("references"), ReferenceValues);
    OutBlock->SetObjectField(TEXT("asset_references"), ReferenceBlock);
    OutFingerprint = Kind + TEXT("\n") + Tag->ToString() + TEXT("\n") + OutFingerprint;
    return true;
}

TSharedRef<FUnrealMCPRecord> AttributeMetadata(const FProperty& Property)
{
    const TSharedRef<FUnrealMCPRecord> Metadata = MakeShared<FUnrealMCPRecord>();
    for (const TCHAR* Name : {TEXT("ClampMin"), TEXT("ClampMax"), TEXT("UIMin"), TEXT("UIMax")})
    {
        if (Property.HasMetaData(Name))
        {
            Metadata->SetStringField(Name, Property.GetMetaData(Name));
        }
    }
    return Metadata;
}

TSharedRef<FUnrealMCPRecord> EncodeAttributeReference(const FGameplayAttribute& Attribute)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    const FProperty* Property = Attribute.GetUProperty();
    const UClass* Owner = Attribute.IsValid() ? Attribute.GetAttributeSetClass() : nullptr;
    Result->SetBoolField(TEXT("resolved"), Property != nullptr && Owner != nullptr);
    Result->SetStringField(TEXT("name"), Attribute.IsValid() ? Attribute.GetName() : FString());
    Result->SetStringField(TEXT("property_path"), Property != nullptr ? Property->GetPathName() : FString());
    Result->SetStringField(TEXT("attribute_set_class"), Owner != nullptr ? Owner->GetPathName() : FString());
    Result->SetStringField(TEXT("attribute_set_asset"), BlueprintAssetPath(Owner));
    return Result;
}

bool BuildAttributeSetBlock(
    UObject& Object,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    UAttributeSet* Defaults = Cast<UAttributeSet>(&Object);
    if (Defaults == nullptr)
    {
        SetError(OutError, TEXT("invalid_asset"),
            TEXT("The asset does not represent an Attribute Set class"));
        return false;
    }
    TArray<FGameplayAttribute> Attributes;
    UAttributeSet::GetAttributesFromSetClass(Defaults->GetClass(), Attributes);
    if (Attributes.Num() > UnrealMCPGAS::MaxSupportingAttributes)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("Attribute Set attributes exceed the output bound"));
        return false;
    }
    Attributes.Sort([](const FGameplayAttribute& Left, const FGameplayAttribute& Right)
    {
        const FString LeftPath = Left.GetUProperty() != nullptr
            ? Left.GetUProperty()->GetPathName() : FString();
        const FString RightPath = Right.GetUProperty() != nullptr
            ? Right.GetUProperty()->GetPathName() : FString();
        return LeftPath < RightPath;
    });
    TArray<TSharedPtr<FUnrealMCPValue>> Values;
    for (const FGameplayAttribute& Attribute : Attributes)
    {
        FProperty* Property = Attribute.GetUProperty();
        if (!Attribute.IsValid() || Property == nullptr)
        {
            continue;
        }
        const TSharedRef<FUnrealMCPRecord> Value = MakeShared<FUnrealMCPRecord>();
        const FString PropertyPath = Property->GetPathName();
        Value->SetStringField(TEXT("attribute_id"), StableSupportingIdentity(PropertyPath));
        Value->SetStringField(TEXT("name"), Attribute.GetName());
        Value->SetStringField(TEXT("property_path"), PropertyPath);
        Value->SetStringField(TEXT("owner_class"), Attribute.GetAttributeSetClass()->GetPathName());
        Value->SetStringField(TEXT("source"), PropertySource(
            *Defaults, Property, UAttributeSet::StaticClass()));
        Value->SetStringField(TEXT("value_type"),
            FGameplayAttribute::IsGameplayAttributeDataProperty(Property)
                ? TEXT("gameplay_attribute_data") : TEXT("numeric"));
        Value->SetBoolField(TEXT("replicated"), Property->HasAnyPropertyFlags(CPF_Net));
        Value->SetBoolField(TEXT("rep_notify"), Property->HasAnyPropertyFlags(CPF_RepNotify));
        Value->SetStringField(TEXT("rep_notify_function"), Property->RepNotifyFunc.ToString());
        Value->SetNumberField(TEXT("current_value"), Attribute.GetNumericValue(Defaults));
        if (const FGameplayAttributeData* Data = Attribute.GetGameplayAttributeData(Defaults))
        {
            Value->SetNumberField(TEXT("base_value"), Data->GetBaseValue());
        }
        else
        {
            Value->SetNumberField(TEXT("base_value"), Attribute.GetNumericValue(Defaults));
        }
        Value->SetObjectField(TEXT("clamp_metadata"), AttributeMetadata(*Property));
        Values.Add(MakeShared<FUnrealMCPValueObject>(Value));

        FString Encoded;
        Property->ExportText_InContainer(0, Encoded, Defaults,
            ParentDefaults(*Defaults, UAttributeSet::StaticClass()), Defaults, PPF_None);
        OutFingerprint += PropertyPath + TEXT("|") + Encoded + TEXT("|")
            + LexToString(static_cast<uint64>(Property->GetPropertyFlags())) + TEXT("\n");
    }
    const TSharedRef<FUnrealMCPRecord> Summary = MakeShared<FUnrealMCPRecord>();
    Summary->SetStringField(TEXT("class_path"), Defaults->GetClass()->GetPathName());
    Summary->SetStringField(TEXT("parent_class_path"),
        Defaults->GetClass()->GetSuperClass() != nullptr
            ? Defaults->GetClass()->GetSuperClass()->GetPathName() : FString());
    Summary->SetNumberField(TEXT("attribute_count"), Values.Num());
    Summary->SetBoolField(TEXT("net_addressable"), Defaults->IsNameStableForNetworking());
    OutBlock->SetObjectField(TEXT("summary"), Summary);
    const TSharedRef<FUnrealMCPRecord> AttributeBlock = MakeShared<FUnrealMCPRecord>();
    AttributeBlock->SetNumberField(TEXT("total_count"), Values.Num());
    AttributeBlock->SetArrayField(TEXT("attributes"), Values);
    OutBlock->SetObjectField(TEXT("attributes"), AttributeBlock);
    OutFingerprint = Defaults->GetClass()->GetPathName() + TEXT("\n") + OutFingerprint;
    return true;
}

TSharedRef<FUnrealMCPRecord> EncodeCapture(
    const FGameplayEffectAttributeCaptureDefinition& Capture,
    int32 DuplicateOrdinal)
{
    const FString Source = Capture.AttributeSource == EGameplayEffectAttributeCaptureSource::Source
        ? TEXT("source") : TEXT("target");
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    const TSharedRef<FUnrealMCPRecord> Attribute = EncodeAttributeReference(Capture.AttributeToCapture);
    FString AttributePath;
    Attribute->TryGetStringField(TEXT("property_path"), AttributePath);
    const FString Seed = AttributePath + TEXT("|") + Source + TEXT("|")
        + (Capture.bSnapshot ? TEXT("1") : TEXT("0"));
    Result->SetStringField(TEXT("capture_id"), StableSupportingIdentity(
        Seed + TEXT("|") + FString::FromInt(DuplicateOrdinal)));
    Result->SetObjectField(TEXT("attribute"), Attribute);
    Result->SetStringField(TEXT("capture_source"), Source);
    Result->SetBoolField(TEXT("snapshot"), Capture.bSnapshot);
    Result->SetBoolField(TEXT("duplicate"), DuplicateOrdinal > 0);
    return Result;
}

bool EncodeCaptures(
    const TArray<FGameplayEffectAttributeCaptureDefinition>& Captures,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutValues,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    if (Captures.Num() > UnrealMCPGAS::MaxSupportingCaptures)
    {
        SetError(OutError, TEXT("response_too_large"),
            TEXT("GAS calculation captures exceed the output bound"));
        return false;
    }
    TMap<FString, int32> Duplicates;
    for (const FGameplayEffectAttributeCaptureDefinition& Capture : Captures)
    {
        const FProperty* Property = Capture.AttributeToCapture.GetUProperty();
        const FString Path = Property != nullptr ? Property->GetPathName() : FString();
        const FString Source = Capture.AttributeSource == EGameplayEffectAttributeCaptureSource::Source
            ? TEXT("source") : TEXT("target");
        const FString Seed = Path + TEXT("|") + Source + TEXT("|")
            + (Capture.bSnapshot ? TEXT("1") : TEXT("0"));
        const int32 Duplicate = Duplicates.FindOrAdd(Seed)++;
        OutValues.Add(MakeShared<FUnrealMCPValueObject>(EncodeCapture(Capture, Duplicate)));
        OutFingerprint += Seed + TEXT("|") + FString::FromInt(Duplicate) + TEXT("\n");
    }
    return true;
}

bool BuildCalculationBlock(
    UObject& Object,
    ESupportingFamily Family,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    UGameplayEffectCalculation* Calculation = Cast<UGameplayEffectCalculation>(&Object);
    if (Calculation == nullptr)
    {
        SetError(OutError, TEXT("invalid_asset"),
            TEXT("The asset does not represent a supported Gameplay Effect calculation class"));
        return false;
    }
    const TSharedRef<FUnrealMCPRecord> Summary = MakeShared<FUnrealMCPRecord>();
    Summary->SetStringField(TEXT("class_path"), Calculation->GetClass()->GetPathName());
    Summary->SetStringField(TEXT("parent_class_path"),
        Calculation->GetClass()->GetSuperClass() != nullptr
            ? Calculation->GetClass()->GetSuperClass()->GetPathName() : FString());
    Summary->SetStringField(TEXT("calculation_kind"),
        Family == ESupportingFamily::MagnitudeCalculation ? TEXT("magnitude") : TEXT("execution"));
    OutBlock->SetObjectField(TEXT("summary"), Summary);

    TArray<TSharedPtr<FUnrealMCPValue>> Captures;
    if (!EncodeCaptures(Calculation->GetAttributeCaptureDefinitions(),
        Captures, OutFingerprint, OutError))
    {
        return false;
    }
    const TSharedRef<FUnrealMCPRecord> CaptureBlock = MakeShared<FUnrealMCPRecord>();
    CaptureBlock->SetNumberField(TEXT("capture_count"), Captures.Num());
    CaptureBlock->SetArrayField(TEXT("captures"), Captures);
    OutBlock->SetObjectField(TEXT("captures"), CaptureBlock);

    const TSharedRef<FUnrealMCPRecord> Policy = MakeShared<FUnrealMCPRecord>();
    if (Family == ESupportingFamily::MagnitudeCalculation)
    {
        const UGameplayModMagnitudeCalculation* Magnitude =
            Cast<UGameplayModMagnitudeCalculation>(Calculation);
        if (Magnitude == nullptr)
        {
            SetError(OutError, TEXT("invalid_asset"),
                TEXT("The asset does not represent a magnitude calculation class"));
            return false;
        }
        const bool bAllow = Magnitude->ShouldAllowNonNetAuthorityDependencyRegistration();
        Policy->SetBoolField(TEXT("allow_non_authority_dependency_registration"), bAllow);
        OutFingerprint += FString::Printf(TEXT("allow_non_authority|%d\n"), bAllow ? 1 : 0);
    }
    else
    {
        const UGameplayEffectExecutionCalculation* Execution =
            Cast<UGameplayEffectExecutionCalculation>(Calculation);
        if (Execution == nullptr)
        {
            SetError(OutError, TEXT("invalid_asset"),
                TEXT("The asset does not represent an execution calculation class"));
            return false;
        }
#if WITH_EDITORONLY_DATA
        const bool bRequiresTags = Execution->DoesRequirePassedInTags();
        Policy->SetBoolField(TEXT("requires_passed_in_tags"), bRequiresTags);
        TArray<FGameplayEffectAttributeCaptureDefinition> ScopedCaptures;
        Execution->GetValidScopedModifierAttributeCaptureDefinitions(ScopedCaptures);
        TArray<TSharedPtr<FUnrealMCPValue>> ScopedValues;
        if (!EncodeCaptures(ScopedCaptures, ScopedValues, OutFingerprint, OutError))
        {
            return false;
        }
        Policy->SetArrayField(TEXT("valid_scoped_modifier_captures"), ScopedValues);
        Policy->SetObjectField(TEXT("valid_transient_aggregator_identifiers"),
            EncodeTags(Execution->GetValidTransientAggregatorIdentifiers()));
        OutFingerprint += FString::Printf(TEXT("requires_passed_tags|%d\n"),
            bRequiresTags ? 1 : 0);
        TArray<FGameplayTag> Tags;
        Execution->GetValidTransientAggregatorIdentifiers().GetGameplayTagArray(Tags);
        Tags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
        { return Left.ToString() < Right.ToString(); });
        for (const FGameplayTag& Tag : Tags)
        {
            OutFingerprint += TEXT("transient_tag|") + Tag.ToString() + TEXT("\n");
        }
#else
        Policy->SetBoolField(TEXT("editor_only_policy_available"), false);
#endif
    }
    OutBlock->SetObjectField(TEXT("policy"), Policy);
    OutFingerprint = Calculation->GetClass()->GetPathName() + TEXT("\n") + OutFingerprint;
    return true;
}

const UClass* RequiredClass(ESupportingFamily Family)
{
    switch (Family)
    {
    case ESupportingFamily::CueStatic: return UGameplayCueNotify_Static::StaticClass();
    case ESupportingFamily::CueActor: return AGameplayCueNotify_Actor::StaticClass();
    case ESupportingFamily::AttributeSet: return UAttributeSet::StaticClass();
    case ESupportingFamily::MagnitudeCalculation:
        return UGameplayModMagnitudeCalculation::StaticClass();
    case ESupportingFamily::ExecutionCalculation:
        return UGameplayEffectExecutionCalculation::StaticClass();
    }
    return nullptr;
}

FString SectionName(ESupportingFamily Family)
{
    switch (Family)
    {
    case ESupportingFamily::CueStatic:
    case ESupportingFamily::CueActor: return CueSection;
    case ESupportingFamily::AttributeSet: return AttributeSection;
    case ESupportingFamily::MagnitudeCalculation: return MagnitudeSection;
    case ESupportingFamily::ExecutionCalculation: return ExecutionSection;
    }
    return FString();
}

bool BuildBlock(
    UObject& Defaults,
    ESupportingFamily Family,
    const TSharedRef<FUnrealMCPRecord>& OutBlock,
    FString& OutFingerprint,
    FUnrealMCPError& OutError)
{
    switch (Family)
    {
    case ESupportingFamily::CueStatic:
    case ESupportingFamily::CueActor:
        return BuildCueBlock(Defaults, RequiredClass(Family), OutBlock, OutFingerprint, OutError);
    case ESupportingFamily::AttributeSet:
        return BuildAttributeSetBlock(Defaults, OutBlock, OutFingerprint, OutError);
    case ESupportingFamily::MagnitudeCalculation:
    case ESupportingFamily::ExecutionCalculation:
        return BuildCalculationBlock(Defaults, Family, OutBlock, OutFingerprint, OutError);
    }
    SetError(OutError, TEXT("invalid_asset"), TEXT("The supporting GAS family is invalid"));
    return false;
}

class FSupportingAssetInspectionAdapter final
    : public IUnrealMCPAssetFamilyInspectionAdapter
{
public:
    explicit FSupportingAssetInspectionAdapter(ESupportingFamily InFamily)
        : Family(InFamily)
    {
    }

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
                TEXT("Supporting GAS selectors do not support paging or graph flags"));
            return false;
        }
        const FString Section = SectionName(Family);
        if (!Selectors.Register({Section, {Section}, false, false}, OutError)
            || !Selectors.Freeze(OutError))
        {
            return false;
        }
        if (!Context.Selector.IsRoot()
            && Selectors.Resolve(Context.Selector, OutError) == nullptr)
        {
            return false;
        }
        UObject* Defaults = ResolveBlueprintDefaults(Context.Asset, RequiredClass(Family));
        if (Defaults == nullptr)
        {
            SetError(OutError, TEXT("invalid_asset"),
                TEXT("The asset does not represent the selected supporting GAS class"));
            return false;
        }
        const TSharedRef<FUnrealMCPRecord> Block = MakeShared<FUnrealMCPRecord>();
        FString Fingerprint;
        if (!BuildBlock(*Defaults, Family, Block, Fingerprint, OutError))
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
    ESupportingFamily Family;
};

FUnrealMCPCompanionAssetFamily MakeFamily(
    ESupportingFamily FamilyKind,
    const TCHAR* FamilyId,
    const UClass* NativeClass)
{
    FUnrealMCPCompanionAssetFamily Family;
    const FString Section = SectionName(FamilyKind);
    Family.FamilyId = FamilyId;
    Family.NativeClassPath = NativeClass->GetPathName();
    Family.ClassPolicy = EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived;
    Family.Priority = 210;
    Family.RequiredModules = {TEXT("GameplayAbilities")};
    Family.Bounds.MaxDocumentBytes = 4 * 1024 * 1024;
    Family.Bounds.MaxValueNodes = 65536;
    Family.Limits = {
        {TEXT("records"), UnrealMCPGAS::MaxSupportingInspectionRecords},
        {TEXT("properties"), UnrealMCPGAS::MaxSupportingProperties},
        {TEXT("references"), UnrealMCPGAS::MaxSupportingReferences},
        {TEXT("attributes"), UnrealMCPGAS::MaxSupportingAttributes},
        {TEXT("captures"), UnrealMCPGAS::MaxSupportingCaptures},
        {TEXT("collection_scan"), UnrealMCPGAS::MaxSupportingCollectionScan},
        {TEXT("property_bytes"), UnrealMCPGAS::MaxSupportingPropertyBytes},
        {TEXT("traversal_depth"), UnrealMCPGAS::MaxSupportingTraversalDepth}};
    Family.Capabilities.bInspection = true;
    Family.SelectorRoutes = {{Section, {Section}, false, false}};
    Family.StableNestedIdentityKinds = {
        TEXT("attribute"), TEXT("capture"), TEXT("asset_reference")};
    Family.InspectionAdapter = MakeShared<FSupportingAssetInspectionAdapter>(FamilyKind);
    Family.SnapshotBuilder = [FamilyKind, NativeClass](UObject* Asset)
    {
        UObject* Defaults = ResolveBlueprintDefaults(Asset, NativeClass);
        if (Defaults == nullptr)
        {
            return FString();
        }
        const TSharedRef<FUnrealMCPRecord> Block = MakeShared<FUnrealMCPRecord>();
        FString Fingerprint;
        FUnrealMCPError Error;
        return BuildBlock(*Defaults, FamilyKind, Block, Fingerprint, Error)
            ? Fingerprint : FString();
    };
    return Family;
}

#if WITH_DEV_AUTOMATION_TESTS
UBlueprint* CreateBlueprintFixture(
    const TCHAR* PackageName,
    const TCHAR* AssetName,
    UClass* ParentClass)
{
    UPackage* Package = CreatePackage(PackageName);
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        ParentClass, Package, FName(AssetName), BPTYPE_Normal,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    if (Blueprint != nullptr)
    {
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
    }
    return Blueprint;
}

bool SaveBlueprint(UBlueprint* Blueprint)
{
    if (Blueprint == nullptr)
    {
        return false;
    }
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    SaveArgs.bSlowTask = false;
    return UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, SaveArgs);
}

bool RemoveFixture(const FString& PackageName)
{
    const FString Filename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());
    return !IFileManager::Get().FileExists(*Filename)
        || IFileManager::Get().Delete(*Filename, false, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPGASSupportingAssetInspectionTest,
    "UnrealMCP.GAS.SupportingAssetInspection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPGASSupportingAssetInspectionTest::RunTest(const FString& Parameters)
{
    UPackage* Package = CreatePackage(TEXT("/Engine/Transient/UnrealMCPGASSupportingAssets"));
    struct FFixture
    {
        const TCHAR* Name;
        UClass* Parent;
        ESupportingFamily Family;
        const TCHAR* Section;
    };
    const TArray<FFixture> Fixtures = {
        {TEXT("GCN_Static"), UGameplayCueNotify_Burst::StaticClass(),
            ESupportingFamily::CueStatic, CueSection},
        {TEXT("GCN_Actor"), AGameplayCueNotify_Looping::StaticClass(),
            ESupportingFamily::CueActor, CueSection},
        {TEXT("AS_Test"), UAttributeSet::StaticClass(),
            ESupportingFamily::AttributeSet, AttributeSection},
        {TEXT("MMC_Test"), UGameplayModMagnitudeCalculation::StaticClass(),
            ESupportingFamily::MagnitudeCalculation, MagnitudeSection},
        {TEXT("Exec_Test"), UGameplayEffectExecutionCalculation::StaticClass(),
            ESupportingFamily::ExecutionCalculation, ExecutionSection}};
    for (const FFixture& Fixture : Fixtures)
    {
        UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
            Fixture.Parent, Package, FName(Fixture.Name), BPTYPE_Normal,
            UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        TestNotNull(FString::Printf(TEXT("%s Blueprint is created"), Fixture.Name), Blueprint);
        if (Blueprint == nullptr)
        {
            return false;
        }
        if (Fixture.Family == ESupportingFamily::AttributeSet)
        {
            FEdGraphPinType AttributeType;
            AttributeType.PinCategory = UEdGraphSchema_K2::PC_Struct;
            AttributeType.PinSubCategoryObject = FGameplayAttributeData::StaticStruct();
            TestTrue(TEXT("Attribute Set fixture declares a gameplay attribute"),
                FBlueprintEditorUtils::AddMemberVariable(
                    Blueprint, TEXT("Health"), AttributeType));
        }
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        const bool bDirtyBefore = Package->IsDirty();
        FSupportingAssetInspectionAdapter Adapter(Fixture.Family);
        FUnrealMCPAssetFamilyInspectionContext Context;
        Context.Asset = Blueprint;
        Context.Identity = {Blueprint->GetPathName(), TEXT("fixture_snapshot")};
        FUnrealMCPAssetFamilyDocumentBuilder FirstDocument{FUnrealMCPAssetFamilyLimits()};
        FUnrealMCPAssetFamilySelectorRouter FirstSelectors{FUnrealMCPAssetFamilyLimits()};
        FUnrealMCPAssetFamilySnapshotBuilder FirstSnapshot{FUnrealMCPAssetFamilyLimits()};
        FUnrealMCPError Error;
        TestTrue(FString::Printf(TEXT("%s root inspection succeeds"), Fixture.Name),
            Adapter.Inspect(Context, FirstDocument, FirstSelectors, FirstSnapshot, Error));
        TestTrue(FString::Printf(TEXT("%s semantic block is present"), Fixture.Name),
            FirstDocument.GetRecords().ContainsByPredicate([Fixture](
                const FUnrealMCPAssetFamilyValueRecord& Value)
            { return Value.Path == Fixture.Section; }));
        TestEqual(FString::Printf(TEXT("%s inspection preserves package dirtiness"), Fixture.Name),
            Package->IsDirty(), bDirtyBefore);

        Context.Selector.Segments = {Fixture.Section};
        FUnrealMCPAssetFamilyDocumentBuilder SelectedDocument{FUnrealMCPAssetFamilyLimits()};
        FUnrealMCPAssetFamilySelectorRouter SelectedSelectors{FUnrealMCPAssetFamilyLimits()};
        FUnrealMCPAssetFamilySnapshotBuilder SelectedSnapshot{FUnrealMCPAssetFamilyLimits()};
        TestTrue(FString::Printf(TEXT("%s selector inspection succeeds"), Fixture.Name),
            Adapter.Inspect(Context, SelectedDocument, SelectedSelectors, SelectedSnapshot, Error));
        TestTrue(FString::Printf(TEXT("%s selector publishes selection"), Fixture.Name),
            SelectedDocument.GetRecords().ContainsByPredicate([](
                const FUnrealMCPAssetFamilyValueRecord& Value)
            { return Value.Path == TEXT("selection"); }));
    }
    return true;
}
#endif
}

namespace UnrealMCPGAS
{
TArray<FUnrealMCPCompanionAssetFamily> MakeSupportingAssetInspectionFamilies()
{
    TArray<FUnrealMCPCompanionAssetFamily> Families;
    Families.Add(MakeFamily(ESupportingFamily::CueStatic,
        TEXT("gameplay_cue_notify_static"), UGameplayCueNotify_Static::StaticClass()));
    Families.Add(MakeFamily(ESupportingFamily::CueActor,
        TEXT("gameplay_cue_notify_actor"), AGameplayCueNotify_Actor::StaticClass()));
    Families.Add(MakeFamily(ESupportingFamily::AttributeSet,
        TEXT("attribute_set"), UAttributeSet::StaticClass()));
    Families.Add(MakeFamily(ESupportingFamily::MagnitudeCalculation,
        TEXT("gameplay_mod_magnitude_calculation"),
        UGameplayModMagnitudeCalculation::StaticClass()));
    Families.Add(MakeFamily(ESupportingFamily::ExecutionCalculation,
        TEXT("gameplay_effect_execution_calculation"),
        UGameplayEffectExecutionCalculation::StaticClass()));
    return Families;
}

#if WITH_DEV_AUTOMATION_TESTS
bool PrepareSupportingAssetLiveFixtures(FString& OutFixtureList)
{
    struct FFixture
    {
        const TCHAR* PackageName;
        const TCHAR* AssetName;
        UClass* Parent;
    };
    const TArray<FFixture> Fixtures = {
        {TEXT("/Game/UnrealMCPGAS/GCN_StaticFixture"), TEXT("GCN_StaticFixture"),
            UGameplayCueNotify_Burst::StaticClass()},
        {TEXT("/Game/UnrealMCPGAS/GCN_ActorFixture"), TEXT("GCN_ActorFixture"),
            AGameplayCueNotify_Looping::StaticClass()},
        {TEXT("/Game/UnrealMCPGAS/AS_InspectionFixture"), TEXT("AS_InspectionFixture"),
            UAttributeSet::StaticClass()},
        {TEXT("/Game/UnrealMCPGAS/MMC_InspectionFixture"), TEXT("MMC_InspectionFixture"),
            UGameplayModMagnitudeCalculation::StaticClass()},
        {TEXT("/Game/UnrealMCPGAS/Exec_InspectionFixture"), TEXT("Exec_InspectionFixture"),
            UGameplayEffectExecutionCalculation::StaticClass()}};
    TArray<FString> Paths;
    for (const FFixture& Fixture : Fixtures)
    {
        if (!RemoveFixture(Fixture.PackageName))
        {
            return false;
        }
        UBlueprint* Blueprint = CreateBlueprintFixture(
            Fixture.PackageName, Fixture.AssetName, Fixture.Parent);
        if (Blueprint == nullptr)
        {
            return false;
        }
        if (Fixture.Parent == UAttributeSet::StaticClass())
        {
            FEdGraphPinType AttributeType;
            AttributeType.PinCategory = UEdGraphSchema_K2::PC_Struct;
            AttributeType.PinSubCategoryObject = FGameplayAttributeData::StaticStruct();
            if (!FBlueprintEditorUtils::AddMemberVariable(
                Blueprint, TEXT("Health"), AttributeType))
            {
                return false;
            }
            FKismetEditorUtilities::CompileBlueprint(Blueprint);
        }
        if (!SaveBlueprint(Blueprint))
        {
            return false;
        }
        Paths.Add(Blueprint->GetPathName());
    }
    OutFixtureList = FString::Join(Paths, TEXT(","));
    return true;
}
#endif
}
