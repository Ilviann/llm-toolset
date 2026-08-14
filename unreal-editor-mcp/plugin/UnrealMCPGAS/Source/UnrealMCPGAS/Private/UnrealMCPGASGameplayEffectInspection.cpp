#include "IUnrealMCPModule.h"
#include "UnrealMCPCompanionApi.h"
#include "UnrealMCPGASVersion.h"

#include "Abilities/GameplayAbility.h"
#include "AttributeSet.h"
#include "UnrealMCPWireTypes.h"
#include "Engine/Blueprint.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponent.h"
#include "GameplayEffectComponents/AbilitiesGameplayEffectComponent.h"
#include "GameplayEffectComponents/AdditionalEffectsGameplayEffectComponent.h"
#include "GameplayEffectComponents/AssetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/BlockAbilityTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/CancelAbilityTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/ChanceToApplyGameplayEffectComponent.h"
#include "GameplayEffectComponents/CustomCanApplyGameplayEffectComponent.h"
#include "GameplayEffectComponents/ImmunityGameplayEffectComponent.h"
#include "GameplayEffectComponents/RemoveOtherGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectCustomApplicationRequirement.h"
#include "GameplayEffectExecutionCalculation.h"
#include "GameplayModMagnitudeCalculation.h"
#include "GameplayTagContainer.h"
#include "Misc/SecureHash.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#endif

namespace
{
constexpr TCHAR GameplayEffectOperation[] = TEXT("inspect_gameplay_effect");

void SetEffectError(FUnrealMCPExtensionError& OutError, const TCHAR* Code, const TCHAR* Message)
{
    OutError.Code = Code;
    OutError.Message = Message;
    OutError.Details = MakeShared<FUnrealMCPRecord>();
}

FString StableEffectIdentity(const FString& Seed)
{
    return FMD5::HashAnsiString(*Seed).ToLower();
}

FString StableEnumName(const UEnum* Enum, int64 Value)
{
    if (Enum == nullptr)
    {
        return TEXT("unknown");
    }
    FString Name = Enum->GetNameStringByValue(Value);
    if (Name.IsEmpty())
    {
        return TEXT("unknown");
    }
    FString Result;
    Result.Reserve(Name.Len() + 8);
    for (int32 Index = 0; Index < Name.Len(); ++Index)
    {
        const TCHAR Character = Name[Index];
        if (FChar::IsUpper(Character) && Index > 0
            && (FChar::IsLower(Name[Index - 1]) || FChar::IsDigit(Name[Index - 1])))
        {
            Result.AppendChar(TEXT('_'));
        }
        Result.AppendChar(FChar::ToLower(Character));
    }
    return Result;
}

const UGameplayEffect* ParentEffectDefaults(const UGameplayEffect& Effect)
{
    const UClass* SuperClass = Effect.GetClass()->GetSuperClass();
    return SuperClass != nullptr && SuperClass->IsChildOf(UGameplayEffect::StaticClass())
        ? Cast<UGameplayEffect>(SuperClass->GetDefaultObject(false)) : nullptr;
}

const FProperty* ExactEffectProperty(const UGameplayEffect& Effect, const TCHAR* Name)
{
    return Effect.GetClass()->FindPropertyByName(FName(Name));
}

FString EffectPropertySource(const UGameplayEffect& Effect, const FProperty* Property)
{
    const UGameplayEffect* Parent = ParentEffectDefaults(Effect);
    return Property != nullptr && Parent != nullptr
        && Property->Identical_InContainer(&Effect, Parent)
        ? TEXT("inherited") : TEXT("local");
}

TSharedRef<FUnrealMCPRecord> StringValue(const FString& Value, const FString& Source)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("value"), Value);
    Result->SetStringField(TEXT("source"), Source);
    return Result;
}

TSharedRef<FUnrealMCPRecord> BoolValue(bool Value, const FString& Source)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetBoolField(TEXT("value"), Value);
    Result->SetStringField(TEXT("source"), Source);
    return Result;
}

TSharedRef<FUnrealMCPRecord> NumberValue(double Value, const FString& Source)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetNumberField(TEXT("value"), Value);
    Result->SetStringField(TEXT("source"), Source);
    return Result;
}

TSharedRef<FUnrealMCPRecord> EncodeClassReference(const UClass* Class, const UClass* RequiredBase)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetBoolField(TEXT("resolved"), Class != nullptr);
    Result->SetBoolField(TEXT("compatible"), Class == nullptr
        || (RequiredBase != nullptr && Class->IsChildOf(RequiredBase)));
    Result->SetStringField(TEXT("class_path"), Class != nullptr ? Class->GetPathName() : FString());
    FString AssetPath;
    if (Class != nullptr)
    {
        if (const UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
        {
            AssetPath = Blueprint->GetPathName();
        }
    }
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    return Result;
}

TSharedRef<FUnrealMCPRecord> EncodeAttribute(const FGameplayAttribute& Attribute)
{
    const FProperty* Property = Attribute.GetUProperty();
    const bool bCompatible = Property != nullptr && FGameplayAttribute::IsSupportedProperty(Property);
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetBoolField(TEXT("resolved"), Attribute.IsValid());
    Result->SetBoolField(TEXT("compatible"), bCompatible);
    Result->SetStringField(TEXT("name"), Attribute.GetName());
    Result->SetStringField(TEXT("property_path"), Property != nullptr ? Property->GetPathName() : FString());
    const UStruct* Owner = Property != nullptr ? Property->GetOwnerStruct() : nullptr;
    Result->SetStringField(TEXT("owner_path"), Owner != nullptr ? Owner->GetPathName() : FString());
    return Result;
}

bool EncodeTags(
    const FGameplayTagContainer& Container,
    TSharedRef<FUnrealMCPRecord> Result,
    FUnrealMCPExtensionError& OutError)
{
    TArray<FGameplayTag> Tags;
    Container.GetGameplayTagArray(Tags);
    if (Tags.Num() > UnrealMCPGAS::MaxTagScan)
    {
        SetEffectError(OutError, TEXT("response_too_large"),
            TEXT("A Gameplay Effect tag container exceeds the scan bound"));
        return false;
    }
    Tags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
    {
        return Left.ToString() < Right.ToString();
    });
    const int32 Count = FMath::Min(Tags.Num(), UnrealMCPGAS::MaxTagsPerContainer);
    TArray<TSharedPtr<FUnrealMCPValue>> Values;
    Values.Reserve(Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        const TSharedRef<FUnrealMCPRecord> Tag = MakeShared<FUnrealMCPRecord>();
        Tag->SetStringField(TEXT("tag"), Tags[Index].ToString());
        Tag->SetBoolField(TEXT("resolved"), Tags[Index].IsValid());
        Values.Add(MakeShared<FUnrealMCPValueObject>(Tag));
    }
    Result->SetArrayField(TEXT("tags"), Values);
    Result->SetNumberField(TEXT("tag_count"), Tags.Num());
    Result->SetBoolField(TEXT("truncated"), Tags.Num() > Count);
    return true;
}

bool EncodeTagContainer(
    const FGameplayTagContainer& Container,
    const FString& Source,
    TSharedRef<FUnrealMCPRecord>& OutResult,
    FUnrealMCPExtensionError& OutError)
{
    OutResult = MakeShared<FUnrealMCPRecord>();
    OutResult->SetStringField(TEXT("source"), Source);
    return EncodeTags(Container, OutResult, OutError);
}

bool EncodeInheritedTags(
    const FInheritedTagContainer& Container,
    const FString& Source,
    TSharedRef<FUnrealMCPRecord>& OutResult,
    FUnrealMCPExtensionError& OutError)
{
    OutResult = MakeShared<FUnrealMCPRecord>();
    OutResult->SetStringField(TEXT("source"), Source);
    for (const TPair<const TCHAR*, const FGameplayTagContainer*>& Field : {
        TPair<const TCHAR*, const FGameplayTagContainer*>(TEXT("combined"), &Container.CombinedTags),
        {TEXT("added"), &Container.Added},
        {TEXT("removed"), &Container.Removed}})
    {
        TSharedRef<FUnrealMCPRecord> Value = MakeShared<FUnrealMCPRecord>();
        if (!EncodeTags(*Field.Value, Value, OutError))
        {
            return false;
        }
        OutResult->SetObjectField(Field.Key, Value);
    }
    return true;
}

TSharedRef<FUnrealMCPRecord> EncodeScalableFloat(const FScalableFloat& Value)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetNumberField(TEXT("coefficient"), Value.Value);
    Result->SetStringField(TEXT("curve_table_path"),
        Value.Curve.CurveTable != nullptr ? Value.Curve.CurveTable->GetPathName() : FString());
    Result->SetStringField(TEXT("curve_row"), Value.Curve.RowName.ToString());
    Result->SetStringField(TEXT("data_registry_type"), Value.RegistryType.ToString());
    Result->SetBoolField(TEXT("resolved"), Value.IsValid());
    Result->SetBoolField(TEXT("static"), Value.IsStatic() && !Value.RegistryType.IsValid());
    return Result;
}

TSharedRef<FUnrealMCPRecord> EncodeTagQuery(
    const FGameplayTagQuery& Query,
    FUnrealMCPExtensionError& OutError,
    bool& bOk)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetBoolField(TEXT("empty"), Query.IsEmpty());
    Result->SetStringField(TEXT("description"), Query.GetDescription().Left(1024));
    TArray<FGameplayTag> Tags;
    Query.GetGameplayTagArray(Tags);
    FGameplayTagContainer Container = FGameplayTagContainer::CreateFromArray(Tags);
    bOk = EncodeTags(Container, Result, OutError);
    return Result;
}

bool EncodeTagRequirements(
    const FGameplayTagRequirements& Requirements,
    TSharedRef<FUnrealMCPRecord>& OutResult,
    FUnrealMCPExtensionError& OutError)
{
    OutResult = MakeShared<FUnrealMCPRecord>();
    TSharedRef<FUnrealMCPRecord> Required = MakeShared<FUnrealMCPRecord>();
    TSharedRef<FUnrealMCPRecord> Ignored = MakeShared<FUnrealMCPRecord>();
    if (!EncodeTags(Requirements.RequireTags, Required, OutError)
        || !EncodeTags(Requirements.IgnoreTags, Ignored, OutError))
    {
        return false;
    }
    bool bQueryOk = true;
    OutResult->SetObjectField(TEXT("required"), Required);
    OutResult->SetObjectField(TEXT("ignored"), Ignored);
    OutResult->SetObjectField(TEXT("query"), EncodeTagQuery(Requirements.TagQuery, OutError, bQueryOk));
    return bQueryOk;
}

TSharedRef<FUnrealMCPRecord> EncodeCapture(const FGameplayEffectAttributeCaptureDefinition& Capture)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetObjectField(TEXT("attribute"), EncodeAttribute(Capture.AttributeToCapture));
    Result->SetStringField(TEXT("capture_source"), StableEnumName(
        StaticEnum<EGameplayEffectAttributeCaptureSource>(),
        static_cast<int64>(Capture.AttributeSource)));
    Result->SetBoolField(TEXT("snapshot"), Capture.bSnapshot);
    return Result;
}

template<typename T>
const T* ExactStructField(const UStruct* Owner, const void* Container, const TCHAR* Name)
{
    const FStructProperty* Property = FindFProperty<FStructProperty>(Owner, Name);
    return Property != nullptr && Property->Struct == T::StaticStruct()
        ? Property->ContainerPtrToValuePtr<T>(Container) : nullptr;
}

TSharedRef<FUnrealMCPRecord> UnsupportedMagnitude(const FString& Reason)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetBoolField(TEXT("supported"), false);
    Result->SetStringField(TEXT("reason"), Reason);
    return Result;
}

bool EncodeMagnitude(
    const FGameplayEffectModifierMagnitude& Magnitude,
    TSharedRef<FUnrealMCPRecord>& OutResult,
    FUnrealMCPExtensionError& OutError)
{
    OutResult = MakeShared<FUnrealMCPRecord>();
    OutResult->SetBoolField(TEXT("supported"), true);
    const EGameplayEffectMagnitudeCalculation Type = Magnitude.GetMagnitudeCalculationType();
    OutResult->SetStringField(TEXT("type"), StableEnumName(
        StaticEnum<EGameplayEffectMagnitudeCalculation>(), static_cast<int64>(Type)));
    const UScriptStruct* Owner = FGameplayEffectModifierMagnitude::StaticStruct();
    switch (Type)
    {
    case EGameplayEffectMagnitudeCalculation::ScalableFloat:
    {
        const FScalableFloat* Value = ExactStructField<FScalableFloat>(
            Owner, &Magnitude, TEXT("ScalableFloatMagnitude"));
        if (Value == nullptr)
        {
            OutResult = UnsupportedMagnitude(TEXT("scalable_float_layout_unavailable"));
            return true;
        }
        OutResult->SetObjectField(TEXT("scalable_float"), EncodeScalableFloat(*Value));
        return true;
    }
    case EGameplayEffectMagnitudeCalculation::AttributeBased:
    {
        const FAttributeBasedFloat* Value = ExactStructField<FAttributeBasedFloat>(
            Owner, &Magnitude, TEXT("AttributeBasedMagnitude"));
        if (Value == nullptr)
        {
            OutResult = UnsupportedMagnitude(TEXT("attribute_based_layout_unavailable"));
            return true;
        }
        const TSharedRef<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
        Data->SetObjectField(TEXT("coefficient"), EncodeScalableFloat(Value->Coefficient));
        Data->SetObjectField(TEXT("pre_multiply_additive"), EncodeScalableFloat(Value->PreMultiplyAdditiveValue));
        Data->SetObjectField(TEXT("post_multiply_additive"), EncodeScalableFloat(Value->PostMultiplyAdditiveValue));
        Data->SetObjectField(TEXT("backing_attribute"), EncodeCapture(Value->BackingAttribute));
        Data->SetStringField(TEXT("calculation_type"), StableEnumName(
            StaticEnum<EAttributeBasedFloatCalculationType>(),
            static_cast<int64>(Value->AttributeCalculationType)));
        Data->SetStringField(TEXT("final_channel"), StableEnumName(
            StaticEnum<EGameplayModEvaluationChannel>(), static_cast<int64>(Value->FinalChannel)));
        Data->SetStringField(TEXT("curve_table_path"), Value->AttributeCurve.CurveTable != nullptr
            ? Value->AttributeCurve.CurveTable->GetPathName() : FString());
        Data->SetStringField(TEXT("curve_row"), Value->AttributeCurve.RowName.ToString());
        TSharedRef<FUnrealMCPRecord> SourceFilter = MakeShared<FUnrealMCPRecord>();
        TSharedRef<FUnrealMCPRecord> TargetFilter = MakeShared<FUnrealMCPRecord>();
        if (!EncodeTags(Value->SourceTagFilter, SourceFilter, OutError)
            || !EncodeTags(Value->TargetTagFilter, TargetFilter, OutError))
        {
            return false;
        }
        Data->SetObjectField(TEXT("source_tag_filter"), SourceFilter);
        Data->SetObjectField(TEXT("target_tag_filter"), TargetFilter);
        OutResult->SetObjectField(TEXT("attribute_based"), Data);
        return true;
    }
    case EGameplayEffectMagnitudeCalculation::CustomCalculationClass:
    {
        const FCustomCalculationBasedFloat* Value = ExactStructField<FCustomCalculationBasedFloat>(
            Owner, &Magnitude, TEXT("CustomMagnitude"));
        if (Value == nullptr)
        {
            OutResult = UnsupportedMagnitude(TEXT("custom_calculation_layout_unavailable"));
            return true;
        }
        const TSharedRef<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
        Data->SetObjectField(TEXT("calculation_class"), EncodeClassReference(
            Value->CalculationClassMagnitude.Get(), UGameplayModMagnitudeCalculation::StaticClass()));
        Data->SetObjectField(TEXT("coefficient"), EncodeScalableFloat(Value->Coefficient));
        Data->SetObjectField(TEXT("pre_multiply_additive"), EncodeScalableFloat(Value->PreMultiplyAdditiveValue));
        Data->SetObjectField(TEXT("post_multiply_additive"), EncodeScalableFloat(Value->PostMultiplyAdditiveValue));
        Data->SetStringField(TEXT("curve_table_path"), Value->FinalLookupCurve.CurveTable != nullptr
            ? Value->FinalLookupCurve.CurveTable->GetPathName() : FString());
        Data->SetStringField(TEXT("curve_row"), Value->FinalLookupCurve.RowName.ToString());
        OutResult->SetObjectField(TEXT("custom_calculation"), Data);
        return true;
    }
    case EGameplayEffectMagnitudeCalculation::SetByCaller:
    {
        const FSetByCallerFloat& Value = Magnitude.GetSetByCallerFloat();
        const TSharedRef<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
        Data->SetStringField(TEXT("data_name"), Value.DataName.ToString());
        Data->SetStringField(TEXT("data_tag"), Value.DataTag.ToString());
        Data->SetBoolField(TEXT("tag_resolved"), Value.DataTag.IsValid());
        OutResult->SetObjectField(TEXT("set_by_caller"), Data);
        return true;
    }
    default:
        OutResult = UnsupportedMagnitude(TEXT("unsupported_magnitude_form"));
        return true;
    }
}

FString ArrayItemSource(const UGameplayEffect& Effect, const TCHAR* PropertyName, int32 Index)
{
    const FArrayProperty* Property = FindFProperty<FArrayProperty>(Effect.GetClass(), PropertyName);
    const UGameplayEffect* Parent = ParentEffectDefaults(Effect);
    if (Property == nullptr || Parent == nullptr)
    {
        return TEXT("local");
    }
    FScriptArrayHelper Child(Property, Property->ContainerPtrToValuePtr<void>(&Effect));
    FScriptArrayHelper ParentArray(Property, Property->ContainerPtrToValuePtr<void>(Parent));
    return Child.IsValidIndex(Index) && ParentArray.IsValidIndex(Index)
        && Property->Inner->Identical(Child.GetRawPtr(Index), ParentArray.GetRawPtr(Index))
        ? TEXT("inherited") : TEXT("local");
}

bool ReadEffectComponents(
    const UGameplayEffect& Effect,
    TArray<const UGameplayEffectComponent*>& OutComponents,
    const FArrayProperty*& OutProperty,
    FUnrealMCPExtensionError& OutError)
{
    OutProperty = FindFProperty<FArrayProperty>(Effect.GetClass(), TEXT("GEComponents"));
    const FObjectPropertyBase* Inner = OutProperty != nullptr
        ? CastField<FObjectPropertyBase>(OutProperty->Inner) : nullptr;
    if (OutProperty == nullptr || Inner == nullptr
        || !Inner->PropertyClass->IsChildOf(UGameplayEffectComponent::StaticClass()))
    {
        SetEffectError(OutError, TEXT("extension_contract_violation"),
            TEXT("The Gameplay Effect component collection is unavailable or incompatible"));
        return false;
    }
    FScriptArrayHelper Array(OutProperty, OutProperty->ContainerPtrToValuePtr<void>(&Effect));
    if (Array.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
    {
        SetEffectError(OutError, TEXT("response_too_large"),
            TEXT("Gameplay Effect components exceed the scan bound"));
        return false;
    }
    OutComponents.Reserve(Array.Num());
    for (int32 Index = 0; Index < Array.Num(); ++Index)
    {
        OutComponents.Add(Cast<UGameplayEffectComponent>(
            Inner->GetObjectPropertyValue(Array.GetRawPtr(Index))));
    }
    return true;
}

bool ObjectsHaveIdenticalInspectableProperties(const UObject& Left, const UObject& Right)
{
    if (Left.GetClass() != Right.GetClass())
    {
        return false;
    }
    for (TFieldIterator<FProperty> Property(Left.GetClass(), EFieldIteratorFlags::IncludeSuper); Property; ++Property)
    {
        if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
        {
            continue;
        }
        if (!Property->Identical_InContainer(&Left, &Right))
        {
            return false;
        }
    }
    return true;
}

FString ComponentSource(
    const UGameplayEffect& Effect,
    const UGameplayEffectComponent& Component,
    int32 ClassOrdinal,
    FUnrealMCPExtensionError& OutError)
{
    const UGameplayEffect* Parent = ParentEffectDefaults(Effect);
    if (Parent == nullptr)
    {
        return TEXT("local");
    }
    TArray<const UGameplayEffectComponent*> ParentComponents;
    const FArrayProperty* ParentProperty = nullptr;
    if (!ReadEffectComponents(*Parent, ParentComponents, ParentProperty, OutError))
    {
        return TEXT("local");
    }
    int32 Seen = 0;
    for (const UGameplayEffectComponent* Candidate : ParentComponents)
    {
        if (Candidate != nullptr && Candidate->GetClass() == Component.GetClass())
        {
            if (Seen++ == ClassOrdinal)
            {
                return ObjectsHaveIdenticalInspectableProperties(Component, *Candidate)
                    ? TEXT("inherited") : TEXT("local");
            }
        }
    }
    return TEXT("local");
}

template<typename StructType>
const FArrayProperty* ExactStructArrayProperty(
    const UObject& Owner,
    const TCHAR* PropertyName)
{
    const FArrayProperty* Property = FindFProperty<FArrayProperty>(Owner.GetClass(), PropertyName);
    const FStructProperty* Inner = Property != nullptr
        ? CastField<FStructProperty>(Property->Inner) : nullptr;
    return Inner != nullptr && Inner->Struct == StructType::StaticStruct() ? Property : nullptr;
}

FString ComponentFieldSource(const FString& ComponentSourceValue)
{
    return ComponentSourceValue;
}

bool AddRequirementRecord(
    TArray<TSharedPtr<FUnrealMCPValue>>& Records,
    const TSharedRef<FUnrealMCPRecord>& Value,
    FUnrealMCPExtensionError& OutError)
{
    if (Records.Num() >= UnrealMCPGAS::MaxGameplayEffectRequirements)
    {
        SetEffectError(OutError, TEXT("response_too_large"),
            TEXT("Gameplay Effect requirements exceed the output bound"));
        return false;
    }
    Records.Add(MakeShared<FUnrealMCPValueObject>(Value));
    return true;
}

TSharedRef<FUnrealMCPRecord> EncodeEffectQuery(
    const FGameplayEffectQuery& Query,
    FUnrealMCPExtensionError& OutError,
    bool& bOk)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetBoolField(TEXT("empty"), Query.IsEmpty());
    Result->SetObjectField(TEXT("owning_tags"), EncodeTagQuery(Query.OwningTagQuery, OutError, bOk));
    if (!bOk) return Result;
    Result->SetObjectField(TEXT("effect_tags"), EncodeTagQuery(Query.EffectTagQuery, OutError, bOk));
    if (!bOk) return Result;
    Result->SetObjectField(TEXT("source_spec_tags"), EncodeTagQuery(Query.SourceTagQuery, OutError, bOk));
    if (!bOk) return Result;
    Result->SetObjectField(TEXT("source_aggregate_tags"), EncodeTagQuery(Query.SourceAggregateTagQuery, OutError, bOk));
    Result->SetObjectField(TEXT("modifying_attribute"), EncodeAttribute(Query.ModifyingAttribute));
    Result->SetStringField(TEXT("effect_source_path"),
        Query.EffectSource != nullptr ? Query.EffectSource->GetPathName() : FString());
    Result->SetObjectField(TEXT("effect_definition"), EncodeClassReference(
        Query.EffectDefinition.Get(), UGameplayEffect::StaticClass()));
    Result->SetBoolField(TEXT("custom_match_supported"), false);
    Result->SetBoolField(TEXT("has_native_custom_match"), Query.CustomMatchDelegate.IsBound());
    Result->SetBoolField(TEXT("has_blueprint_custom_match"), Query.CustomMatchDelegate_BP.IsBound());
    return Result;
}

bool AppendClassReferenceArray(
    const UObject& Owner,
    const TCHAR* PropertyName,
    const FString& Channel,
    const FString& ParentId,
    const FString& Source,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutReferences,
    TMap<FString, int32>& DuplicateCounts,
    FUnrealMCPExtensionError& OutError)
{
    const FArrayProperty* Property = FindFProperty<FArrayProperty>(Owner.GetClass(), PropertyName);
    const FClassProperty* Inner = Property != nullptr ? CastField<FClassProperty>(Property->Inner) : nullptr;
    if (Property == nullptr || Inner == nullptr)
    {
        SetEffectError(OutError, TEXT("extension_contract_violation"),
            TEXT("A required Gameplay Effect class-reference collection is unavailable"));
        return false;
    }
    FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(&Owner));
    if (Array.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
    {
        SetEffectError(OutError, TEXT("response_too_large"),
            TEXT("Gameplay Effect references exceed the scan bound"));
        return false;
    }
    for (int32 Index = 0; Index < Array.Num(); ++Index)
    {
        if (OutReferences.Num() >= UnrealMCPGAS::MaxGameplayEffectReferences)
        {
            SetEffectError(OutError, TEXT("response_too_large"),
                TEXT("Gameplay Effect references exceed the output bound"));
            return false;
        }
        UClass* Class = Cast<UClass>(Inner->GetObjectPropertyValue(Array.GetRawPtr(Index)));
        const FString Key = Channel + TEXT("|") + (Class != nullptr ? Class->GetPathName() : TEXT("unresolved"));
        const int32 Duplicate = DuplicateCounts.FindOrAdd(Key)++;
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("reference_id"), StableEffectIdentity(
            ParentId + TEXT("|") + Key + TEXT("|") + FString::FromInt(Duplicate)));
        Record->SetStringField(TEXT("channel"), Channel);
        Record->SetStringField(TEXT("owner_id"), ParentId);
        Record->SetStringField(TEXT("source"), Source);
        Record->SetObjectField(TEXT("effect"), EncodeClassReference(Class, UGameplayEffect::StaticClass()));
        OutReferences.Add(MakeShared<FUnrealMCPValueObject>(Record));
    }
    return true;
}

bool AppendConditionalEffects(
    const TArray<FConditionalGameplayEffect>& Effects,
    const FString& Channel,
    const FString& ParentId,
    const FString& Source,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutReferences,
    TMap<FString, int32>& DuplicateCounts,
    FUnrealMCPExtensionError& OutError)
{
    if (Effects.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
    {
        SetEffectError(OutError, TEXT("response_too_large"),
            TEXT("Conditional Gameplay Effects exceed the scan bound"));
        return false;
    }
    for (const FConditionalGameplayEffect& Effect : Effects)
    {
        if (OutReferences.Num() >= UnrealMCPGAS::MaxGameplayEffectReferences)
        {
            SetEffectError(OutError, TEXT("response_too_large"),
                TEXT("Gameplay Effect references exceed the output bound"));
            return false;
        }
        UClass* Class = Effect.EffectClass.Get();
        const FString Key = Channel + TEXT("|") + (Class != nullptr ? Class->GetPathName() : TEXT("unresolved"));
        const int32 Duplicate = DuplicateCounts.FindOrAdd(Key)++;
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("reference_id"), StableEffectIdentity(
            ParentId + TEXT("|") + Key + TEXT("|") + FString::FromInt(Duplicate)));
        Record->SetStringField(TEXT("channel"), Channel);
        Record->SetStringField(TEXT("owner_id"), ParentId);
        Record->SetStringField(TEXT("source"), Source);
        Record->SetObjectField(TEXT("effect"), EncodeClassReference(Class, UGameplayEffect::StaticClass()));
        TSharedRef<FUnrealMCPRecord> RequiredTags = MakeShared<FUnrealMCPRecord>();
        if (!EncodeTags(Effect.RequiredSourceTags, RequiredTags, OutError))
        {
            return false;
        }
        Record->SetObjectField(TEXT("required_source_tags"), RequiredTags);
        Record->SetStringField(TEXT("removal_policy"), StableEnumName(
            StaticEnum<EGameplayEffectGrantedEffectRemovalPolicy>(),
            static_cast<int64>(Effect.RemovalPolicy)));
        Record->SetNumberField(TEXT("stack_count_to_remove"), Effect.StackCountToRemove);
        OutReferences.Add(MakeShared<FUnrealMCPValueObject>(Record));
    }
    return true;
}

void AddRelationship(
    TArray<TSharedPtr<FUnrealMCPValue>>& Relationships,
    const FString& Code,
    const FString& Severity,
    const FString& Message)
{
    if (Relationships.Num() >= UnrealMCPGAS::MaxGameplayEffectRelationships)
    {
        return;
    }
    const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
    Record->SetStringField(TEXT("relationship_id"), StableEffectIdentity(Code + TEXT("|") + Message));
    Record->SetStringField(TEXT("code"), Code);
    Record->SetStringField(TEXT("severity"), Severity);
    Record->SetStringField(TEXT("message"), Message);
    Relationships.Add(MakeShared<FUnrealMCPValueObject>(Record));
}

bool AppendReferencedClasses(
    const UObject& Owner,
    const TCHAR* PropertyName,
    TArray<UClass*>& OutClasses,
    FUnrealMCPExtensionError& OutError)
{
    const FArrayProperty* Property = FindFProperty<FArrayProperty>(Owner.GetClass(), PropertyName);
    const FClassProperty* Inner = Property != nullptr ? CastField<FClassProperty>(Property->Inner) : nullptr;
    if (Property == nullptr || Inner == nullptr)
    {
        SetEffectError(OutError, TEXT("extension_contract_violation"),
            TEXT("A required chained-effect collection is unavailable"));
        return false;
    }
    FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(&Owner));
    if (Array.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
    {
        SetEffectError(OutError, TEXT("response_too_large"),
            TEXT("Chained Gameplay Effect references exceed the scan bound"));
        return false;
    }
    for (int32 Index = 0; Index < Array.Num(); ++Index)
    {
        if (UClass* Class = Cast<UClass>(Inner->GetObjectPropertyValue(Array.GetRawPtr(Index))))
        {
            OutClasses.Add(Class);
        }
    }
    return true;
}

bool CollectDirectEffectReferences(
    const UGameplayEffect& Effect,
    TArray<UClass*>& OutClasses,
    FUnrealMCPExtensionError& OutError)
{
    if (!AppendReferencedClasses(Effect, TEXT("OverflowEffects"), OutClasses, OutError))
    {
        return false;
    }
    if (Effect.Executions.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
    {
        SetEffectError(OutError, TEXT("response_too_large"),
            TEXT("Chained execution references exceed the scan bound"));
        return false;
    }
    for (const FGameplayEffectExecutionDefinition& Execution : Effect.Executions)
    {
        if (Execution.ConditionalGameplayEffects.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
        {
            SetEffectError(OutError, TEXT("response_too_large"),
                TEXT("Conditional chained effects exceed the scan bound"));
            return false;
        }
        for (const FConditionalGameplayEffect& Conditional : Execution.ConditionalGameplayEffects)
        {
            if (Conditional.EffectClass != nullptr)
            {
                OutClasses.Add(Conditional.EffectClass.Get());
            }
        }
    }
    TArray<const UGameplayEffectComponent*> Components;
    const FArrayProperty* ComponentsProperty = nullptr;
    if (!ReadEffectComponents(Effect, Components, ComponentsProperty, OutError))
    {
        return false;
    }
    for (const UGameplayEffectComponent* Component : Components)
    {
        const UAdditionalEffectsGameplayEffectComponent* Additional =
            Cast<UAdditionalEffectsGameplayEffectComponent>(Component);
        if (Additional == nullptr)
        {
            continue;
        }
        if (Additional->OnApplicationGameplayEffects.Num()
            > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
        {
            SetEffectError(OutError, TEXT("response_too_large"),
                TEXT("On-application chained effects exceed the scan bound"));
            return false;
        }
        for (const FConditionalGameplayEffect& Conditional : Additional->OnApplicationGameplayEffects)
        {
            if (Conditional.EffectClass != nullptr)
            {
                OutClasses.Add(Conditional.EffectClass.Get());
            }
        }
        for (const TCHAR* PropertyName : {
            TEXT("OnCompleteAlways"), TEXT("OnCompleteNormal"), TEXT("OnCompletePrematurely")})
        {
            if (!AppendReferencedClasses(*Additional, PropertyName, OutClasses, OutError))
            {
                return false;
            }
        }
    }
    if (OutClasses.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
    {
        SetEffectError(OutError, TEXT("response_too_large"),
            TEXT("Direct chained Gameplay Effects exceed the scan bound"));
        return false;
    }
    return true;
}

struct FEffectChainInspection
{
    bool bCycle = false;
    bool bDepthExceeded = false;
    bool bAssetLimitExceeded = false;
    TSet<const UClass*> Visited;
    TSet<const UClass*> Visiting;
    TArray<FString> Edges;
};

bool InspectEffectChain(
    const UClass* Class,
    int32 Depth,
    FEffectChainInspection& State,
    FUnrealMCPExtensionError& OutError)
{
    if (Class == nullptr)
    {
        return true;
    }
    if (State.Visiting.Contains(Class))
    {
        State.bCycle = true;
        return true;
    }
    if (State.Visited.Contains(Class))
    {
        return true;
    }
    if (Depth > UnrealMCPGAS::MaxGameplayEffectChainDepth)
    {
        State.bDepthExceeded = true;
        return true;
    }
    if (State.Visited.Num() >= UnrealMCPGAS::MaxGameplayEffectChainAssets)
    {
        State.bAssetLimitExceeded = true;
        return true;
    }
    const UGameplayEffect* Defaults = Cast<UGameplayEffect>(Class->GetDefaultObject(false));
    if (Defaults == nullptr)
    {
        return true;
    }
    State.Visiting.Add(Class);
    TArray<UClass*> References;
    if (!CollectDirectEffectReferences(*Defaults, References, OutError))
    {
        State.Visiting.Remove(Class);
        return false;
    }
    References.Sort([](const UClass& Left, const UClass& Right)
    {
        return Left.GetPathName() < Right.GetPathName();
    });
    for (const UClass* Reference : References)
    {
        State.Edges.Add(Class->GetPathName() + TEXT("->") + Reference->GetPathName());
        if (State.Visiting.Contains(Reference))
        {
            State.bCycle = true;
            continue;
        }
        if (!InspectEffectChain(Reference, Depth + 1, State, OutError))
        {
            State.Visiting.Remove(Class);
            return false;
        }
    }
    State.Visiting.Remove(Class);
    State.Visited.Add(Class);
    return true;
}

bool BuildGameplayEffectPayload(
    const UGameplayEffect& Effect,
    TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
    FString& OutFingerprint,
    FUnrealMCPExtensionError& OutError)
{
    const FString DurationSource = EffectPropertySource(
        Effect, ExactEffectProperty(Effect, TEXT("DurationPolicy")));
    const TSharedRef<FUnrealMCPRecord> Duration = MakeShared<FUnrealMCPRecord>();
    Duration->SetStringField(TEXT("section"), TEXT("gameplay_effect_duration"));
    Duration->SetObjectField(TEXT("duration_policy"), StringValue(StableEnumName(
        StaticEnum<EGameplayEffectDurationType>(), static_cast<int64>(Effect.DurationPolicy)), DurationSource));
    TSharedRef<FUnrealMCPRecord> DurationMagnitude = MakeShared<FUnrealMCPRecord>();
    TSharedRef<FUnrealMCPRecord> MaxDurationMagnitude = MakeShared<FUnrealMCPRecord>();
    if (!EncodeMagnitude(Effect.DurationMagnitude, DurationMagnitude, OutError)
        || !EncodeMagnitude(Effect.MaxDurationMagnitude, MaxDurationMagnitude, OutError))
    {
        return false;
    }
    DurationMagnitude->SetStringField(TEXT("source"), EffectPropertySource(
        Effect, ExactEffectProperty(Effect, TEXT("DurationMagnitude"))));
    MaxDurationMagnitude->SetStringField(TEXT("source"), EffectPropertySource(
        Effect, ExactEffectProperty(Effect, TEXT("MaxDurationMagnitude"))));
    Duration->SetObjectField(TEXT("duration_magnitude"), DurationMagnitude);
    Duration->SetObjectField(TEXT("max_duration_magnitude"), MaxDurationMagnitude);
    const TSharedRef<FUnrealMCPRecord> Period = EncodeScalableFloat(Effect.Period);
    Period->SetStringField(TEXT("source"), EffectPropertySource(
        Effect, ExactEffectProperty(Effect, TEXT("Period"))));
    Duration->SetObjectField(TEXT("period"), Period);
    Duration->SetObjectField(TEXT("execute_periodic_on_application"), BoolValue(
        Effect.bExecutePeriodicEffectOnApplication, EffectPropertySource(
            Effect, ExactEffectProperty(Effect, TEXT("bExecutePeriodicEffectOnApplication")))));
    Duration->SetObjectField(TEXT("periodic_inhibition_policy"), StringValue(StableEnumName(
        StaticEnum<EGameplayEffectPeriodInhibitionRemovedPolicy>(),
        static_cast<int64>(Effect.PeriodicInhibitionPolicy)), EffectPropertySource(
            Effect, ExactEffectProperty(Effect, TEXT("PeriodicInhibitionPolicy")))));
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(Duration));

    const TSharedRef<FUnrealMCPRecord> Modifiers = MakeShared<FUnrealMCPRecord>();
    Modifiers->SetStringField(TEXT("section"), TEXT("gameplay_effect_modifiers"));
    Modifiers->SetStringField(TEXT("source"), EffectPropertySource(
        Effect, ExactEffectProperty(Effect, TEXT("Modifiers"))));
    Modifiers->SetNumberField(TEXT("modifier_count"), Effect.Modifiers.Num());
    if (Effect.Modifiers.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
    {
        SetEffectError(OutError, TEXT("response_too_large"),
            TEXT("Gameplay Effect modifiers exceed the scan bound"));
        return false;
    }
    TMap<FString, int32> ModifierDuplicates;
    TArray<TSharedPtr<FUnrealMCPValue>> ModifierValues;
    const int32 ModifierCount = FMath::Min(
        Effect.Modifiers.Num(), UnrealMCPGAS::MaxGameplayEffectModifiers);
    for (int32 Index = 0; Index < ModifierCount; ++Index)
    {
        const FGameplayModifierInfo& Modifier = Effect.Modifiers[Index];
        const FString AttributeKey = Modifier.Attribute.GetUProperty() != nullptr
            ? Modifier.Attribute.GetUProperty()->GetPathName() : TEXT("unresolved");
        const int32 Duplicate = ModifierDuplicates.FindOrAdd(AttributeKey)++;
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("modifier_id"), StableEffectIdentity(
            AttributeKey + TEXT("|") + FString::FromInt(Duplicate)));
        Record->SetStringField(TEXT("source"), ArrayItemSource(Effect, TEXT("Modifiers"), Index));
        Record->SetObjectField(TEXT("attribute"), EncodeAttribute(Modifier.Attribute));
        Record->SetStringField(TEXT("operation"), StableEnumName(
            StaticEnum<EGameplayModOp::Type>(), static_cast<int64>(Modifier.ModifierOp.GetValue())));
        TSharedRef<FUnrealMCPRecord> Magnitude = MakeShared<FUnrealMCPRecord>();
        TSharedRef<FUnrealMCPRecord> SourceTags = MakeShared<FUnrealMCPRecord>();
        TSharedRef<FUnrealMCPRecord> TargetTags = MakeShared<FUnrealMCPRecord>();
        if (!EncodeMagnitude(Modifier.ModifierMagnitude, Magnitude, OutError)
            || !EncodeTagRequirements(Modifier.SourceTags, SourceTags, OutError)
            || !EncodeTagRequirements(Modifier.TargetTags, TargetTags, OutError))
        {
            return false;
        }
        Record->SetObjectField(TEXT("magnitude"), Magnitude);
        Record->SetObjectField(TEXT("source_tag_requirements"), SourceTags);
        Record->SetObjectField(TEXT("target_tag_requirements"), TargetTags);
        ModifierValues.Add(MakeShared<FUnrealMCPValueObject>(Record));
    }
    Modifiers->SetArrayField(TEXT("modifiers"), ModifierValues);
    Modifiers->SetBoolField(TEXT("truncated"), Effect.Modifiers.Num() > ModifierCount);
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(Modifiers));

    const TSharedRef<FUnrealMCPRecord> Executions = MakeShared<FUnrealMCPRecord>();
    Executions->SetStringField(TEXT("section"), TEXT("gameplay_effect_executions"));
    Executions->SetStringField(TEXT("source"), EffectPropertySource(
        Effect, ExactEffectProperty(Effect, TEXT("Executions"))));
    Executions->SetNumberField(TEXT("execution_count"), Effect.Executions.Num());
    if (Effect.Executions.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
    {
        SetEffectError(OutError, TEXT("response_too_large"),
            TEXT("Gameplay Effect executions exceed the scan bound"));
        return false;
    }
    TMap<FString, int32> ExecutionDuplicates;
    TArray<TSharedPtr<FUnrealMCPValue>> ExecutionValues;
    TArray<TSharedPtr<FUnrealMCPValue>> AdditionalValues;
    TMap<FString, int32> ReferenceDuplicates;
    const int32 ExecutionCount = FMath::Min(
        Effect.Executions.Num(), UnrealMCPGAS::MaxGameplayEffectExecutions);
    for (int32 Index = 0; Index < ExecutionCount; ++Index)
    {
        const FGameplayEffectExecutionDefinition& Execution = Effect.Executions[Index];
        UClass* CalculationClass = Execution.CalculationClass.Get();
        const FString Key = CalculationClass != nullptr
            ? CalculationClass->GetPathName() : TEXT("unresolved");
        const int32 Duplicate = ExecutionDuplicates.FindOrAdd(Key)++;
        const FString ExecutionId = StableEffectIdentity(Key + TEXT("|") + FString::FromInt(Duplicate));
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("execution_id"), ExecutionId);
        Record->SetStringField(TEXT("source"), ArrayItemSource(Effect, TEXT("Executions"), Index));
        Record->SetObjectField(TEXT("calculation_class"), EncodeClassReference(
            CalculationClass, UGameplayEffectExecutionCalculation::StaticClass()));
        TSharedRef<FUnrealMCPRecord> PassedTags = MakeShared<FUnrealMCPRecord>();
        if (!EncodeTags(Execution.PassedInTags, PassedTags, OutError))
        {
            return false;
        }
        Record->SetObjectField(TEXT("passed_in_tags"), PassedTags);
        Record->SetNumberField(TEXT("scoped_modifier_count"), Execution.CalculationModifiers.Num());
        Record->SetNumberField(TEXT("conditional_effect_count"), Execution.ConditionalGameplayEffects.Num());
        if (Execution.CalculationModifiers.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
        {
            SetEffectError(OutError, TEXT("response_too_large"),
                TEXT("Execution scoped modifiers exceed the scan bound"));
            return false;
        }
        TArray<TSharedPtr<FUnrealMCPValue>> ScopedValues;
        const int32 ScopedCount = FMath::Min(Execution.CalculationModifiers.Num(), 64);
        for (int32 ScopedIndex = 0; ScopedIndex < ScopedCount; ++ScopedIndex)
        {
            const FGameplayEffectExecutionScopedModifierInfo& Scoped = Execution.CalculationModifiers[ScopedIndex];
            const TSharedRef<FUnrealMCPRecord> ScopedRecord = MakeShared<FUnrealMCPRecord>();
            ScopedRecord->SetStringField(TEXT("scoped_modifier_id"), StableEffectIdentity(
                ExecutionId + TEXT("|") + FString::FromInt(ScopedIndex)));
            ScopedRecord->SetObjectField(TEXT("captured_attribute"), EncodeCapture(Scoped.CapturedAttribute));
            ScopedRecord->SetStringField(TEXT("transient_aggregator_tag"), Scoped.TransientAggregatorIdentifier.ToString());
            ScopedRecord->SetStringField(TEXT("aggregator_type"), StableEnumName(
                StaticEnum<EGameplayEffectScopedModifierAggregatorType>(),
                static_cast<int64>(Scoped.AggregatorType)));
            ScopedRecord->SetStringField(TEXT("operation"), StableEnumName(
                StaticEnum<EGameplayModOp::Type>(), static_cast<int64>(Scoped.ModifierOp.GetValue())));
            TSharedRef<FUnrealMCPRecord> Magnitude = MakeShared<FUnrealMCPRecord>();
            TSharedRef<FUnrealMCPRecord> SourceTags = MakeShared<FUnrealMCPRecord>();
            TSharedRef<FUnrealMCPRecord> TargetTags = MakeShared<FUnrealMCPRecord>();
            if (!EncodeMagnitude(Scoped.ModifierMagnitude, Magnitude, OutError)
                || !EncodeTagRequirements(Scoped.SourceTags, SourceTags, OutError)
                || !EncodeTagRequirements(Scoped.TargetTags, TargetTags, OutError))
            {
                return false;
            }
            ScopedRecord->SetObjectField(TEXT("magnitude"), Magnitude);
            ScopedRecord->SetObjectField(TEXT("source_tag_requirements"), SourceTags);
            ScopedRecord->SetObjectField(TEXT("target_tag_requirements"), TargetTags);
            ScopedValues.Add(MakeShared<FUnrealMCPValueObject>(ScopedRecord));
        }
        Record->SetArrayField(TEXT("scoped_modifiers"), ScopedValues);
        Record->SetBoolField(TEXT("scoped_modifiers_truncated"),
            Execution.CalculationModifiers.Num() > ScopedCount);
        if (!AppendConditionalEffects(Execution.ConditionalGameplayEffects,
            TEXT("execution_conditional"), ExecutionId, ArrayItemSource(
                Effect, TEXT("Executions"), Index), AdditionalValues, ReferenceDuplicates, OutError))
        {
            return false;
        }
        ExecutionValues.Add(MakeShared<FUnrealMCPValueObject>(Record));
    }
    Executions->SetArrayField(TEXT("executions"), ExecutionValues);
    Executions->SetBoolField(TEXT("truncated"), Effect.Executions.Num() > ExecutionCount);
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(Executions));

    const TSharedRef<FUnrealMCPRecord> Stacking = MakeShared<FUnrealMCPRecord>();
    Stacking->SetStringField(TEXT("section"), TEXT("gameplay_effect_stacking"));
    Stacking->SetObjectField(TEXT("type"), StringValue(StableEnumName(
        StaticEnum<EGameplayEffectStackingType>(), static_cast<int64>(Effect.GetStackingType())),
        EffectPropertySource(Effect, ExactEffectProperty(Effect, TEXT("StackingType")))));
    Stacking->SetObjectField(TEXT("stack_limit"), NumberValue(Effect.GetStackLimitCount(),
        EffectPropertySource(Effect, ExactEffectProperty(Effect, TEXT("StackLimitCount")))));
    Stacking->SetObjectField(TEXT("duration_refresh_policy"), StringValue(StableEnumName(
        StaticEnum<EGameplayEffectStackingDurationPolicy>(),
        static_cast<int64>(Effect.StackDurationRefreshPolicy)), EffectPropertySource(
            Effect, ExactEffectProperty(Effect, TEXT("StackDurationRefreshPolicy")))));
    Stacking->SetObjectField(TEXT("period_reset_policy"), StringValue(StableEnumName(
        StaticEnum<EGameplayEffectStackingPeriodPolicy>(),
        static_cast<int64>(Effect.StackPeriodResetPolicy)), EffectPropertySource(
            Effect, ExactEffectProperty(Effect, TEXT("StackPeriodResetPolicy")))));
    Stacking->SetObjectField(TEXT("expiration_policy"), StringValue(StableEnumName(
        StaticEnum<EGameplayEffectStackingExpirationPolicy>(),
        static_cast<int64>(Effect.GetStackExpirationPolicy())), EffectPropertySource(
            Effect, ExactEffectProperty(Effect, TEXT("StackExpirationPolicy")))));
    Stacking->SetObjectField(TEXT("factor_in_stack_count"), BoolValue(Effect.bFactorInStackCount,
        EffectPropertySource(Effect, ExactEffectProperty(Effect, TEXT("bFactorInStackCount")))));
    Stacking->SetObjectField(TEXT("deny_overflow_application"), BoolValue(Effect.bDenyOverflowApplication,
        EffectPropertySource(Effect, ExactEffectProperty(Effect, TEXT("bDenyOverflowApplication")))));
    Stacking->SetObjectField(TEXT("clear_stack_on_overflow"), BoolValue(Effect.bClearStackOnOverflow,
        EffectPropertySource(Effect, ExactEffectProperty(Effect, TEXT("bClearStackOnOverflow")))));
    Stacking->SetNumberField(TEXT("overflow_effect_count"), Effect.OverflowEffects.Num());
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(Stacking));
    if (!AppendClassReferenceArray(Effect, TEXT("OverflowEffects"), TEXT("stack_overflow"),
        TEXT("stacking"), EffectPropertySource(Effect, ExactEffectProperty(
            Effect, TEXT("OverflowEffects"))), AdditionalValues, ReferenceDuplicates, OutError))
    {
        return false;
    }

    const TSharedRef<FUnrealMCPRecord> Cues = MakeShared<FUnrealMCPRecord>();
    Cues->SetStringField(TEXT("section"), TEXT("gameplay_effect_cues"));
    Cues->SetStringField(TEXT("source"), EffectPropertySource(
        Effect, ExactEffectProperty(Effect, TEXT("GameplayCues"))));
    Cues->SetNumberField(TEXT("cue_count"), Effect.GameplayCues.Num());
    Cues->SetObjectField(TEXT("require_modifier_success"), BoolValue(
        Effect.bRequireModifierSuccessToTriggerCues, EffectPropertySource(
            Effect, ExactEffectProperty(Effect, TEXT("bRequireModifierSuccessToTriggerCues")))));
    Cues->SetObjectField(TEXT("suppress_stacking_cues"), BoolValue(
        Effect.bSuppressStackingCues, EffectPropertySource(
            Effect, ExactEffectProperty(Effect, TEXT("bSuppressStackingCues")))));
    if (Effect.GameplayCues.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
    {
        SetEffectError(OutError, TEXT("response_too_large"),
            TEXT("Gameplay Effect cues exceed the scan bound"));
        return false;
    }
    TMap<FString, int32> CueDuplicates;
    TArray<TSharedPtr<FUnrealMCPValue>> CueValues;
    const int32 CueCount = FMath::Min(
        Effect.GameplayCues.Num(), UnrealMCPGAS::MaxGameplayEffectCues);
    for (int32 Index = 0; Index < CueCount; ++Index)
    {
        const FGameplayEffectCue& Cue = Effect.GameplayCues[Index];
        TArray<FGameplayTag> CueTags;
        Cue.GameplayCueTags.GetGameplayTagArray(CueTags);
        CueTags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
        {
            return Left.ToString() < Right.ToString();
        });
        TArray<FString> CueTagNames;
        for (const FGameplayTag& Tag : CueTags) CueTagNames.Add(Tag.ToString());
        const FString Key = FString::Join(CueTagNames, TEXT(","));
        const int32 Duplicate = CueDuplicates.FindOrAdd(Key)++;
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        Record->SetStringField(TEXT("cue_id"), StableEffectIdentity(
            Key + TEXT("|") + FString::FromInt(Duplicate)));
        Record->SetStringField(TEXT("source"), ArrayItemSource(Effect, TEXT("GameplayCues"), Index));
        Record->SetObjectField(TEXT("magnitude_attribute"), EncodeAttribute(Cue.MagnitudeAttribute));
        Record->SetNumberField(TEXT("min_level"), Cue.MinLevel);
        Record->SetNumberField(TEXT("max_level"), Cue.MaxLevel);
        TSharedRef<FUnrealMCPRecord> Tags = MakeShared<FUnrealMCPRecord>();
        if (!EncodeTags(Cue.GameplayCueTags, Tags, OutError)) return false;
        Record->SetObjectField(TEXT("tags"), Tags);
        CueValues.Add(MakeShared<FUnrealMCPValueObject>(Record));
    }
    Cues->SetArrayField(TEXT("cues"), CueValues);
    Cues->SetBoolField(TEXT("truncated"), Effect.GameplayCues.Num() > CueCount);
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(Cues));

    const TSharedRef<FUnrealMCPRecord> Tags = MakeShared<FUnrealMCPRecord>();
    Tags->SetStringField(TEXT("section"), TEXT("gameplay_effect_tags"));
    for (const TPair<const TCHAR*, const FGameplayTagContainer*>& Field : {
        TPair<const TCHAR*, const FGameplayTagContainer*>(TEXT("asset"), &Effect.GetAssetTags()),
        {TEXT("granted"), &Effect.GetGrantedTags()},
        {TEXT("blocked_abilities"), &Effect.GetBlockedAbilityTags()}})
    {
        TSharedRef<FUnrealMCPRecord> Value = MakeShared<FUnrealMCPRecord>();
        if (!EncodeTags(*Field.Value, Value, OutError)) return false;
        Value->SetStringField(TEXT("source"), TEXT("effective"));
        Tags->SetObjectField(Field.Key, Value);
    }
    TArray<TSharedPtr<FUnrealMCPValue>> TagComponentValues;
    TArray<TSharedPtr<FUnrealMCPValue>> GrantedAbilityValues;
    TArray<TSharedPtr<FUnrealMCPValue>> RequirementValues;

    TArray<const UGameplayEffectComponent*> Components;
    const FArrayProperty* ComponentsProperty = nullptr;
    if (!ReadEffectComponents(Effect, Components, ComponentsProperty, OutError)) return false;
    const TSharedRef<FUnrealMCPRecord> ComponentRecord = MakeShared<FUnrealMCPRecord>();
    ComponentRecord->SetStringField(TEXT("section"), TEXT("gameplay_effect_components"));
    ComponentRecord->SetStringField(TEXT("source"), EffectPropertySource(Effect, ComponentsProperty));
    ComponentRecord->SetNumberField(TEXT("component_count"), Components.Num());
    TArray<TSharedPtr<FUnrealMCPValue>> ComponentValues;
    TMap<FString, int32> ComponentOrdinals;
    const int32 ComponentCount = FMath::Min(
        Components.Num(), UnrealMCPGAS::MaxGameplayEffectComponents);
    for (int32 Index = 0; Index < ComponentCount; ++Index)
    {
        const UGameplayEffectComponent* Component = Components[Index];
        const FString ClassPath = Component != nullptr ? Component->GetClass()->GetPathName() : TEXT("unresolved");
        const int32 Ordinal = ComponentOrdinals.FindOrAdd(ClassPath)++;
        const FString ComponentId = StableEffectIdentity(
            ClassPath + TEXT("|") + FString::FromInt(Ordinal));
        const TSharedRef<FUnrealMCPRecord> Meta = MakeShared<FUnrealMCPRecord>();
        Meta->SetStringField(TEXT("component_id"), ComponentId);
        Meta->SetStringField(TEXT("class_path"), Component != nullptr ? ClassPath : FString());
        Meta->SetBoolField(TEXT("resolved"), Component != nullptr);
        FString Source = TEXT("local");
        if (Component != nullptr)
        {
            Source = ComponentSource(Effect, *Component, Ordinal, OutError);
            if (!OutError.Code.IsEmpty()) return false;
        }
        Meta->SetStringField(TEXT("source"), Source);
        FString Type = TEXT("unsupported");
        FString DataSection;
        bool bSupported = Component != nullptr;

        if (const UAssetTagsGameplayEffectComponent* AssetTagsComponent = Cast<UAssetTagsGameplayEffectComponent>(Component))
        {
            Type = TEXT("asset_tags"); DataSection = TEXT("gameplay_effect_tags");
            TSharedRef<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
            if (!EncodeInheritedTags(AssetTagsComponent->GetConfiguredAssetTagChanges(), Source, Data, OutError)) return false;
            Data->SetStringField(TEXT("component_id"), ComponentId);
            Data->SetStringField(TEXT("type"), Type);
            TagComponentValues.Add(MakeShared<FUnrealMCPValueObject>(Data));
        }
        else if (const UTargetTagsGameplayEffectComponent* TargetTagsComponent = Cast<UTargetTagsGameplayEffectComponent>(Component))
        {
            Type = TEXT("target_tags"); DataSection = TEXT("gameplay_effect_tags");
            TSharedRef<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
            if (!EncodeInheritedTags(TargetTagsComponent->GetConfiguredTargetTagChanges(), Source, Data, OutError)) return false;
            Data->SetStringField(TEXT("component_id"), ComponentId);
            Data->SetStringField(TEXT("type"), Type);
            TagComponentValues.Add(MakeShared<FUnrealMCPValueObject>(Data));
        }
        else if (const UBlockAbilityTagsGameplayEffectComponent* BlockTagsComponent = Cast<UBlockAbilityTagsGameplayEffectComponent>(Component))
        {
            Type = TEXT("block_ability_tags"); DataSection = TEXT("gameplay_effect_tags");
            TSharedRef<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
            if (!EncodeInheritedTags(BlockTagsComponent->GetConfiguredBlockedAbilityTagChanges(), Source, Data, OutError)) return false;
            Data->SetStringField(TEXT("component_id"), ComponentId);
            Data->SetStringField(TEXT("type"), Type);
            TagComponentValues.Add(MakeShared<FUnrealMCPValueObject>(Data));
        }
        else if (const UTargetTagRequirementsGameplayEffectComponent* TargetRequirementsComponent = Cast<UTargetTagRequirementsGameplayEffectComponent>(Component))
        {
            Type = TEXT("target_tag_requirements"); DataSection = TEXT("gameplay_effect_requirements");
            const TSharedRef<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
            Data->SetStringField(TEXT("requirement_id"), ComponentId);
            Data->SetStringField(TEXT("component_id"), ComponentId);
            Data->SetStringField(TEXT("type"), Type);
            Data->SetStringField(TEXT("source"), Source);
            TSharedRef<FUnrealMCPRecord> Application = MakeShared<FUnrealMCPRecord>();
            TSharedRef<FUnrealMCPRecord> Ongoing = MakeShared<FUnrealMCPRecord>();
            TSharedRef<FUnrealMCPRecord> Removal = MakeShared<FUnrealMCPRecord>();
            if (!EncodeTagRequirements(TargetRequirementsComponent->ApplicationTagRequirements, Application, OutError)
                || !EncodeTagRequirements(TargetRequirementsComponent->OngoingTagRequirements, Ongoing, OutError)
                || !EncodeTagRequirements(TargetRequirementsComponent->RemovalTagRequirements, Removal, OutError)) return false;
            Data->SetObjectField(TEXT("application"), Application);
            Data->SetObjectField(TEXT("ongoing"), Ongoing);
            Data->SetObjectField(TEXT("removal"), Removal);
            if (!AddRequirementRecord(RequirementValues, Data, OutError)) return false;
        }
        else if (const UChanceToApplyGameplayEffectComponent* ChanceComponent = Cast<UChanceToApplyGameplayEffectComponent>(Component))
        {
            Type = TEXT("chance_to_apply"); DataSection = TEXT("gameplay_effect_requirements");
            const TSharedRef<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
            Data->SetStringField(TEXT("requirement_id"), ComponentId);
            Data->SetStringField(TEXT("component_id"), ComponentId);
            Data->SetStringField(TEXT("type"), Type);
            Data->SetStringField(TEXT("source"), Source);
            Data->SetObjectField(TEXT("chance"), EncodeScalableFloat(ChanceComponent->GetChanceToApplyToTarget()));
            if (!AddRequirementRecord(RequirementValues, Data, OutError)) return false;
        }
        else if (const UCustomCanApplyGameplayEffectComponent* CustomApplyComponent = Cast<UCustomCanApplyGameplayEffectComponent>(Component))
        {
            Type = TEXT("custom_can_apply"); DataSection = TEXT("gameplay_effect_requirements");
            const TSharedRef<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
            Data->SetStringField(TEXT("requirement_id"), ComponentId);
            Data->SetStringField(TEXT("component_id"), ComponentId);
            Data->SetStringField(TEXT("type"), Type);
            Data->SetStringField(TEXT("source"), Source);
            TArray<TSharedPtr<FUnrealMCPValue>> Values;
            for (const TSubclassOf<UGameplayEffectCustomApplicationRequirement>& Requirement : CustomApplyComponent->ApplicationRequirements)
            {
                Values.Add(MakeShared<FUnrealMCPValueObject>(EncodeClassReference(
                    Requirement.Get(), UGameplayEffectCustomApplicationRequirement::StaticClass())));
            }
            Data->SetArrayField(TEXT("application_requirements"), Values);
            if (!AddRequirementRecord(RequirementValues, Data, OutError)) return false;
        }
        else if (const UImmunityGameplayEffectComponent* ImmunityComponent = Cast<UImmunityGameplayEffectComponent>(Component))
        {
            Type = TEXT("immunity"); DataSection = TEXT("gameplay_effect_requirements");
            if (ImmunityComponent->ImmunityQueries.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
            {
                SetEffectError(OutError, TEXT("response_too_large"), TEXT("Immunity queries exceed the scan bound"));
                return false;
            }
            for (int32 QueryIndex = 0; QueryIndex < ImmunityComponent->ImmunityQueries.Num(); ++QueryIndex)
            {
                const TSharedRef<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
                Data->SetStringField(TEXT("requirement_id"), StableEffectIdentity(
                    ComponentId + TEXT("|immunity|") + FString::FromInt(QueryIndex)));
                Data->SetStringField(TEXT("component_id"), ComponentId);
                Data->SetStringField(TEXT("type"), Type);
                Data->SetStringField(TEXT("source"), Source);
                bool bOk = true;
                Data->SetObjectField(TEXT("query"), EncodeEffectQuery(ImmunityComponent->ImmunityQueries[QueryIndex], OutError, bOk));
                if (!bOk || !AddRequirementRecord(RequirementValues, Data, OutError)) return false;
            }
        }
        else if (const URemoveOtherGameplayEffectComponent* RemoveComponent = Cast<URemoveOtherGameplayEffectComponent>(Component))
        {
            Type = TEXT("remove_other_effects"); DataSection = TEXT("gameplay_effect_requirements");
            if (RemoveComponent->RemoveGameplayEffectQueries.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
            {
                SetEffectError(OutError, TEXT("response_too_large"), TEXT("Removal queries exceed the scan bound"));
                return false;
            }
            for (int32 QueryIndex = 0; QueryIndex < RemoveComponent->RemoveGameplayEffectQueries.Num(); ++QueryIndex)
            {
                const TSharedRef<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
                Data->SetStringField(TEXT("requirement_id"), StableEffectIdentity(
                    ComponentId + TEXT("|remove|") + FString::FromInt(QueryIndex)));
                Data->SetStringField(TEXT("component_id"), ComponentId);
                Data->SetStringField(TEXT("type"), Type);
                Data->SetStringField(TEXT("source"), Source);
                bool bOk = true;
                Data->SetObjectField(TEXT("query"), EncodeEffectQuery(RemoveComponent->RemoveGameplayEffectQueries[QueryIndex], OutError, bOk));
                if (!bOk || !AddRequirementRecord(RequirementValues, Data, OutError)) return false;
            }
        }
        else if (Cast<UCancelAbilityTagsGameplayEffectComponent>(Component) != nullptr)
        {
            Type = TEXT("cancel_ability_tags"); DataSection = TEXT("gameplay_effect_requirements");
            const FStructProperty* WithProperty = FindFProperty<FStructProperty>(
                Component->GetClass(), TEXT("InheritableCancelAbilitiesWithTagsContainer"));
            const FStructProperty* WithoutProperty = FindFProperty<FStructProperty>(
                Component->GetClass(), TEXT("InheritableCancelAbilitiesWithoutTagsContainer"));
            const FEnumProperty* ModeProperty = FindFProperty<FEnumProperty>(
                Component->GetClass(), TEXT("ComponentMode"));
            if (WithProperty == nullptr || WithoutProperty == nullptr || ModeProperty == nullptr
                || WithProperty->Struct != FInheritedTagContainer::StaticStruct()
                || WithoutProperty->Struct != FInheritedTagContainer::StaticStruct())
            {
                SetEffectError(OutError, TEXT("extension_contract_violation"),
                    TEXT("Cancel Ability Tags component layout is unavailable"));
                return false;
            }
            const TSharedRef<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
            Data->SetStringField(TEXT("requirement_id"), ComponentId);
            Data->SetStringField(TEXT("component_id"), ComponentId);
            Data->SetStringField(TEXT("type"), Type);
            Data->SetStringField(TEXT("source"), Source);
            const void* ModeAddress = ModeProperty->ContainerPtrToValuePtr<void>(Component);
            Data->SetStringField(TEXT("mode"), StableEnumName(ModeProperty->GetEnum(),
                ModeProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ModeAddress)));
            TSharedRef<FUnrealMCPRecord> WithTags = MakeShared<FUnrealMCPRecord>();
            TSharedRef<FUnrealMCPRecord> WithoutTags = MakeShared<FUnrealMCPRecord>();
            if (!EncodeInheritedTags(*WithProperty->ContainerPtrToValuePtr<FInheritedTagContainer>(Component),
                    Source, WithTags, OutError)
                || !EncodeInheritedTags(*WithoutProperty->ContainerPtrToValuePtr<FInheritedTagContainer>(Component),
                    Source, WithoutTags, OutError)) return false;
            Data->SetObjectField(TEXT("with_tags"), WithTags);
            Data->SetObjectField(TEXT("without_tags"), WithoutTags);
            if (!AddRequirementRecord(RequirementValues, Data, OutError)) return false;
        }
        else if (const UAbilitiesGameplayEffectComponent* AbilitiesComponent = Cast<UAbilitiesGameplayEffectComponent>(Component))
        {
            Type = TEXT("granted_abilities"); DataSection = TEXT("gameplay_effect_granted_abilities");
            const FArrayProperty* Property = ExactStructArrayProperty<FGameplayAbilitySpecConfig>(
                *AbilitiesComponent, TEXT("GrantAbilityConfigs"));
            if (Property == nullptr)
            {
                SetEffectError(OutError, TEXT("extension_contract_violation"),
                    TEXT("Granted Ability component layout is unavailable"));
                return false;
            }
            FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(AbilitiesComponent));
            if (Array.Num() > UnrealMCPGAS::MaxGameplayEffectCollectionScan)
            {
                SetEffectError(OutError, TEXT("response_too_large"), TEXT("Granted abilities exceed the scan bound"));
                return false;
            }
            for (int32 AbilityIndex = 0; AbilityIndex < Array.Num(); ++AbilityIndex)
            {
                if (GrantedAbilityValues.Num() >= UnrealMCPGAS::MaxGameplayEffectGrantedAbilities)
                {
                    SetEffectError(OutError, TEXT("response_too_large"), TEXT("Granted abilities exceed the output bound"));
                    return false;
                }
                const FGameplayAbilitySpecConfig* Config =
                    reinterpret_cast<const FGameplayAbilitySpecConfig*>(Array.GetRawPtr(AbilityIndex));
                const FString AbilityPath = Config->Ability != nullptr
                    ? Config->Ability->GetPathName() : TEXT("unresolved");
                const TSharedRef<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
                Data->SetStringField(TEXT("grant_id"), StableEffectIdentity(
                    ComponentId + TEXT("|") + AbilityPath + TEXT("|") + FString::FromInt(AbilityIndex)));
                Data->SetStringField(TEXT("component_id"), ComponentId);
                Data->SetStringField(TEXT("source"), Source);
                Data->SetObjectField(TEXT("ability"), EncodeClassReference(
                    Config->Ability.Get(), UGameplayAbility::StaticClass()));
                Data->SetObjectField(TEXT("level"), EncodeScalableFloat(Config->LevelScalableFloat));
                Data->SetNumberField(TEXT("input_id"), Config->InputID);
                Data->SetStringField(TEXT("removal_policy"), StableEnumName(
                    StaticEnum<EGameplayEffectGrantedAbilityRemovePolicy>(),
                    static_cast<int64>(Config->RemovalPolicy)));
                GrantedAbilityValues.Add(MakeShared<FUnrealMCPValueObject>(Data));
            }
        }
        else if (const UAdditionalEffectsGameplayEffectComponent* AdditionalComponent = Cast<UAdditionalEffectsGameplayEffectComponent>(Component))
        {
            Type = TEXT("additional_effects"); DataSection = TEXT("gameplay_effect_additional_effects");
            if (!AppendConditionalEffects(AdditionalComponent->OnApplicationGameplayEffects, TEXT("on_application"),
                ComponentId, Source, AdditionalValues, ReferenceDuplicates, OutError)
                || !AppendClassReferenceArray(*AdditionalComponent, TEXT("OnCompleteAlways"), TEXT("on_complete_always"),
                    ComponentId, Source, AdditionalValues, ReferenceDuplicates, OutError)
                || !AppendClassReferenceArray(*AdditionalComponent, TEXT("OnCompleteNormal"), TEXT("on_complete_normal"),
                    ComponentId, Source, AdditionalValues, ReferenceDuplicates, OutError)
                || !AppendClassReferenceArray(*AdditionalComponent, TEXT("OnCompletePrematurely"), TEXT("on_complete_prematurely"),
                    ComponentId, Source, AdditionalValues, ReferenceDuplicates, OutError)) return false;
        }
        else
        {
            bSupported = false;
        }
        Meta->SetStringField(TEXT("type"), Type);
        Meta->SetBoolField(TEXT("supported"), bSupported);
        Meta->SetStringField(TEXT("data_section"), DataSection);
        if (!bSupported)
        {
            Meta->SetStringField(TEXT("unsupported_reason"), Component == nullptr
                ? TEXT("unresolved_component") : TEXT("unsupported_component_class"));
        }
        ComponentValues.Add(MakeShared<FUnrealMCPValueObject>(Meta));
    }
    ComponentRecord->SetArrayField(TEXT("components"), ComponentValues);
    ComponentRecord->SetBoolField(TEXT("truncated"), Components.Num() > ComponentCount);
    Tags->SetArrayField(TEXT("component_tag_data"), TagComponentValues);
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(Tags));

    const TSharedRef<FUnrealMCPRecord> GrantedAbilities = MakeShared<FUnrealMCPRecord>();
    GrantedAbilities->SetStringField(TEXT("section"), TEXT("gameplay_effect_granted_abilities"));
    GrantedAbilities->SetArrayField(TEXT("granted_abilities"), GrantedAbilityValues);
    GrantedAbilities->SetNumberField(TEXT("grant_count"), GrantedAbilityValues.Num());
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(GrantedAbilities));

    const TSharedRef<FUnrealMCPRecord> Additional = MakeShared<FUnrealMCPRecord>();
    Additional->SetStringField(TEXT("section"), TEXT("gameplay_effect_additional_effects"));
    Additional->SetArrayField(TEXT("references"), AdditionalValues);
    Additional->SetNumberField(TEXT("reference_count"), AdditionalValues.Num());
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(Additional));

    const TSharedRef<FUnrealMCPRecord> Requirements = MakeShared<FUnrealMCPRecord>();
    Requirements->SetStringField(TEXT("section"), TEXT("gameplay_effect_requirements"));
    Requirements->SetArrayField(TEXT("requirements"), RequirementValues);
    Requirements->SetNumberField(TEXT("requirement_count"), RequirementValues.Num());
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(Requirements));
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(ComponentRecord));

    TArray<TSharedPtr<FUnrealMCPValue>> Relationships;
    const bool bPeriodic = !Effect.Period.IsStatic() || !FMath::IsNearlyZero(Effect.Period.Value);
    if (Effect.DurationPolicy == EGameplayEffectDurationType::Instant && bPeriodic)
        AddRelationship(Relationships, TEXT("instant_effect_has_period"), TEXT("error"),
            TEXT("Instant Gameplay Effects cannot execute periodically"));
    if (!bPeriodic && Effect.bExecutePeriodicEffectOnApplication)
        AddRelationship(Relationships, TEXT("periodic_on_application_without_period"), TEXT("warning"),
            TEXT("Periodic-on-application is enabled while the period is zero"));
    if (Effect.DurationPolicy == EGameplayEffectDurationType::Instant
        && Effect.GetStackingType() != EGameplayEffectStackingType::None)
        AddRelationship(Relationships, TEXT("instant_effect_stacking"), TEXT("error"),
            TEXT("Instant Gameplay Effects cannot retain stacks"));
    if (Effect.GetStackingType() == EGameplayEffectStackingType::None
        && Effect.OverflowEffects.Num() > 0)
        AddRelationship(Relationships, TEXT("overflow_without_stacking"), TEXT("error"),
            TEXT("Overflow effects require an enabled stacking policy"));
    if (Effect.bClearStackOnOverflow && !Effect.bDenyOverflowApplication)
        AddRelationship(Relationships, TEXT("clear_stack_without_denied_overflow"), TEXT("error"),
            TEXT("Clearing a stack on overflow requires denied overflow application"));
    if (Effect.DurationPolicy == EGameplayEffectDurationType::Instant
        && GrantedAbilityValues.Num() > 0)
        AddRelationship(Relationships, TEXT("instant_effect_grants_abilities"), TEXT("error"),
            TEXT("Instant Gameplay Effects cannot own persistent granted abilities"));
    FEffectChainInspection Chain;
    if (!InspectEffectChain(Effect.GetClass(), 0, Chain, OutError))
    {
        return false;
    }
    if (Chain.bCycle)
        AddRelationship(Relationships, TEXT("effect_chain_cycle"), TEXT("error"),
            TEXT("The chained Gameplay Effect graph contains a cycle"));
    if (Chain.bDepthExceeded)
        AddRelationship(Relationships, TEXT("effect_chain_depth_exceeded"), TEXT("error"),
            TEXT("The chained Gameplay Effect graph exceeds the inspection depth bound"));
    if (Chain.bAssetLimitExceeded)
        AddRelationship(Relationships, TEXT("effect_chain_asset_limit_exceeded"), TEXT("error"),
            TEXT("The chained Gameplay Effect graph exceeds the inspected-asset bound"));
    for (const TSharedPtr<FUnrealMCPValue>& Value : ComponentValues)
    {
        const TSharedPtr<FUnrealMCPRecord>* Object = nullptr;
        bool bSupported = true;
        if (Value.IsValid() && Value->TryGetObject(Object) && Object != nullptr
            && (*Object)->TryGetBoolField(TEXT("supported"), bSupported) && !bSupported)
        {
            AddRelationship(Relationships, TEXT("unsupported_component"), TEXT("warning"),
                TEXT("At least one Gameplay Effect Component is outside the inspection allowlist"));
            break;
        }
    }
    const TSharedRef<FUnrealMCPRecord> RelationshipRecord = MakeShared<FUnrealMCPRecord>();
    RelationshipRecord->SetStringField(TEXT("section"), TEXT("gameplay_effect_relationships"));
    RelationshipRecord->SetArrayField(TEXT("relationships"), Relationships);
    RelationshipRecord->SetNumberField(TEXT("relationship_count"), Relationships.Num());
    RelationshipRecord->SetNumberField(TEXT("chain_asset_count"), Chain.Visited.Num());
    RelationshipRecord->SetNumberField(TEXT("chain_edge_count"), Chain.Edges.Num());
    RelationshipRecord->SetBoolField(TEXT("chain_cycle"), Chain.bCycle);
    RelationshipRecord->SetBoolField(TEXT("chain_truncated"),
        Chain.bDepthExceeded || Chain.bAssetLimitExceeded);
    RelationshipRecord->SetBoolField(TEXT("valid"), !Relationships.ContainsByPredicate(
        [](const TSharedPtr<FUnrealMCPValue>& Value)
        {
            const TSharedPtr<FUnrealMCPRecord>* Object = nullptr;
            FString Severity;
            return Value.IsValid() && Value->TryGetObject(Object) && Object != nullptr
                && (*Object)->TryGetStringField(TEXT("severity"), Severity) && Severity == TEXT("error");
        }));
    OutRecords.Add(MakeShared<FUnrealMCPValueObject>(RelationshipRecord));

    if (OutRecords.Num() != UnrealMCPGAS::MaxGameplayEffectInspectionRecords)
    {
        SetEffectError(OutError, TEXT("extension_contract_violation"),
            TEXT("Gameplay Effect inspection emitted an unexpected record count"));
        return false;
    }
    TArray<FString> Fingerprint;
    for (const TCHAR* PropertyName : {
        TEXT("DurationPolicy"), TEXT("DurationMagnitude"), TEXT("MaxDurationMagnitude"),
        TEXT("Period"), TEXT("bExecutePeriodicEffectOnApplication"), TEXT("PeriodicInhibitionPolicy"),
        TEXT("Modifiers"), TEXT("Executions"), TEXT("OverflowEffects"),
        TEXT("bDenyOverflowApplication"), TEXT("bClearStackOnOverflow"),
        TEXT("bRequireModifierSuccessToTriggerCues"), TEXT("bSuppressStackingCues"),
        TEXT("GameplayCues"), TEXT("StackingType"), TEXT("StackLimitCount"),
        TEXT("StackDurationRefreshPolicy"), TEXT("StackPeriodResetPolicy"),
        TEXT("StackExpirationPolicy"), TEXT("bFactorInStackCount"), TEXT("GEComponents")})
    {
        const FProperty* Property = ExactEffectProperty(Effect, PropertyName);
        if (Property == nullptr)
        {
            SetEffectError(OutError, TEXT("extension_contract_violation"),
                TEXT("A required Gameplay Effect fingerprint property is unavailable"));
            return false;
        }
        FString Encoded;
        Property->ExportText_InContainer(0, Encoded, &Effect, ParentEffectDefaults(Effect),
            const_cast<UGameplayEffect*>(&Effect), PPF_None);
        Fingerprint.Add(FString(PropertyName) + TEXT("|") + Encoded);
    }
    for (const TPair<const TCHAR*, const FGameplayTagContainer*>& Field : {
        TPair<const TCHAR*, const FGameplayTagContainer*>(TEXT("effective_asset_tags"), &Effect.GetAssetTags()),
        {TEXT("effective_granted_tags"), &Effect.GetGrantedTags()},
        {TEXT("effective_blocked_ability_tags"), &Effect.GetBlockedAbilityTags()}})
    {
        TArray<FGameplayTag> Values;
        Field.Value->GetGameplayTagArray(Values);
        Values.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
        {
            return Left.ToString() < Right.ToString();
        });
        TArray<FString> Names;
        Names.Reserve(Values.Num());
        for (const FGameplayTag& Tag : Values) Names.Add(Tag.ToString());
        Fingerprint.Add(FString(Field.Key) + TEXT("|") + FString::Join(Names, TEXT(",")));
    }
    Chain.Edges.Sort();
    Fingerprint.Add(TEXT("effect_chain|") + FString::Join(Chain.Edges, TEXT(",")));
    OutFingerprint = FString::Join(Fingerprint, TEXT("\n"));
    return true;
}

class FGameplayEffectInspectionHandler final : public IUnrealMCPExtensionHandler
{
public:
    bool IsReady(FString& OutUnavailableReason) const override
    {
        OutUnavailableReason.Reset();
        return UGameplayEffect::StaticClass() != nullptr
            && UGameplayEffectComponent::StaticClass() != nullptr;
    }

    bool SupportsTarget(const UObject& Target) const override
    {
        return Target.IsA<UGameplayEffect>();
    }

    bool ValidateArguments(
        const FString& Operation,
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        FUnrealMCPExtensionError& OutError) const override
    {
        FString Mode;
        if (Operation != GameplayEffectOperation || !Arguments.IsValid()
            || !Arguments->TryGetStringField(TEXT("mode"), Mode) || Mode != TEXT("inspect"))
        {
            SetEffectError(OutError, TEXT("invalid_argument"),
                TEXT("Gameplay Effect inspection requires the exact inspect operation"));
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
        const UGameplayEffect* Effect = Cast<UGameplayEffect>(&Target);
        TArray<TSharedPtr<FUnrealMCPValue>> Records;
        FString Fingerprint;
        if (Effect == nullptr || !BuildGameplayEffectPayload(*Effect, Records, Fingerprint, OutError))
        {
            if (Effect == nullptr)
            {
                SetEffectError(OutError, TEXT("invalid_asset"),
                    TEXT("The target is not a Gameplay Effect class default object"));
            }
            return false;
        }
        const TArray<TSharedPtr<FUnrealMCPValue>>* RequestedSections = nullptr;
        if (Arguments->TryGetArrayField(TEXT("sections"), RequestedSections)
            && RequestedSections != nullptr)
        {
            const bool bRequested = RequestedSections->ContainsByPredicate(
                [](const TSharedPtr<FUnrealMCPValue>& Value)
                {
                    FString Section;
                    return Value.IsValid() && Value->TryGetString(Section)
                        && Section == TEXT("gameplay_effect");
                });
            if (!bRequested)
            {
                Records.Reset();
            }
        }
        const TSharedRef<FUnrealMCPRecord> Capabilities = MakeShared<FUnrealMCPRecord>();
        Capabilities->SetBoolField(TEXT("inspection"), true);
        Capabilities->SetBoolField(TEXT("mutation"), false);
        Capabilities->SetArrayField(TEXT("magnitude_forms"), {
            MakeShared<FUnrealMCPValueString>(TEXT("scalable_float")),
            MakeShared<FUnrealMCPValueString>(TEXT("attribute_based")),
            MakeShared<FUnrealMCPValueString>(TEXT("custom_calculation_class")),
            MakeShared<FUnrealMCPValueString>(TEXT("set_by_caller"))});
        Capabilities->SetArrayField(TEXT("modifier_operations"), {
            MakeShared<FUnrealMCPValueString>(TEXT("add_base")),
            MakeShared<FUnrealMCPValueString>(TEXT("multiply_additive")),
            MakeShared<FUnrealMCPValueString>(TEXT("divide_additive")),
            MakeShared<FUnrealMCPValueString>(TEXT("override")),
            MakeShared<FUnrealMCPValueString>(TEXT("multiply_compound")),
            MakeShared<FUnrealMCPValueString>(TEXT("add_final"))});
        Capabilities->SetNumberField(TEXT("max_modifiers"), UnrealMCPGAS::MaxGameplayEffectModifiers);
        Capabilities->SetNumberField(TEXT("max_executions"), UnrealMCPGAS::MaxGameplayEffectExecutions);
        Capabilities->SetNumberField(TEXT("max_cues"), UnrealMCPGAS::MaxGameplayEffectCues);
        Capabilities->SetNumberField(TEXT("max_components"), UnrealMCPGAS::MaxGameplayEffectComponents);
        Capabilities->SetNumberField(TEXT("max_effect_references"), UnrealMCPGAS::MaxGameplayEffectReferences);
        Capabilities->SetNumberField(TEXT("max_collection_scan"), UnrealMCPGAS::MaxGameplayEffectCollectionScan);
        Capabilities->SetNumberField(TEXT("max_chain_depth"), UnrealMCPGAS::MaxGameplayEffectChainDepth);
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
        const UGameplayEffect* Effect = Cast<UGameplayEffect>(&Target);
        TArray<TSharedPtr<FUnrealMCPValue>> Records;
        return Effect != nullptr
            && BuildGameplayEffectPayload(*Effect, Records, OutFingerprint, OutError);
    }

    bool ApplyMutation(
        UObject&, const FString&, const TSharedPtr<FUnrealMCPRecord>&,
        TSharedPtr<FUnrealMCPRecord>&, FUnrealMCPExtensionError& OutError) override
    {
        SetEffectError(OutError, TEXT("extension_unavailable"),
            TEXT("Gameplay Effect inspection exposes no mutation operation"));
        return false;
    }

    bool ReadBack(
        const UObject&, const FString&, const TSharedPtr<FUnrealMCPRecord>&,
        TSharedPtr<FUnrealMCPRecord>&, FUnrealMCPExtensionError& OutError) const override
    {
        SetEffectError(OutError, TEXT("extension_unavailable"),
            TEXT("Gameplay Effect inspection exposes no mutation read-back"));
        return false;
    }
};

UGameplayEffect* ResolveGameplayEffectDefaults(UObject* Asset)
{
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
    UClass* GeneratedClass = Blueprint != nullptr
        ? (Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass : Blueprint->ParentClass)
        : nullptr;
    return GeneratedClass != nullptr
        ? Cast<UGameplayEffect>(GeneratedClass->GetDefaultObject(false)) : nullptr;
}

void ConvertGameplayEffectInspectionError(
    const FUnrealMCPExtensionError& Input,
    FUnrealMCPError& Output)
{
    Output.Code = Input.Code;
    Output.Message = Input.Message;
    Output.Details = Input.Details;
    Output.bRetryable = Input.bRetryable;
}

class FGameplayEffectAssetInspectionAdapter final
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
        if (Context.bHasPaging || Context.bHasPartialGraphFlag)
        {
            OutError = {TEXT("invalid_argument"),
                TEXT("Gameplay Effect selectors do not support paging or graph flags")};
            return false;
        }
        if (!Selectors.Register(
            {TEXT("gameplay_effect"), {TEXT("gameplay_effect")}, false, false}, OutError)
            || !Selectors.Freeze(OutError)
            || (!Context.Selector.IsRoot()
                && Selectors.Resolve(Context.Selector, OutError) == nullptr))
        {
            return false;
        }
        UGameplayEffect* Effect = ResolveGameplayEffectDefaults(Context.Asset);
        if (Effect == nullptr)
        {
            OutError = {TEXT("invalid_asset"),
                TEXT("The asset does not represent a Gameplay Effect class")};
            return false;
        }
        const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
        Arguments->SetStringField(TEXT("mode"), TEXT("inspect"));
        Arguments->SetArrayField(TEXT("sections"), {
            MakeShared<FUnrealMCPValueString>(TEXT("gameplay_effect"))});
        TSharedPtr<FUnrealMCPRecord> Result;
        FUnrealMCPExtensionError ExtensionError;
        if (!Handler.Inspect(*Effect, GameplayEffectOperation, Arguments, Result, ExtensionError))
        {
            ConvertGameplayEffectInspectionError(ExtensionError, OutError);
            return false;
        }
        const TArray<TSharedPtr<FUnrealMCPValue>>* Records = nullptr;
        if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("records"), Records)
            || Records == nullptr
            || Records->Num() != UnrealMCPGAS::MaxGameplayEffectInspectionRecords)
        {
            OutError = {TEXT("extension_contract_violation"),
                TEXT("The GAS adapter returned an invalid Gameplay Effect block")};
            return false;
        }
        const TSharedRef<FUnrealMCPRecord> Block = MakeShared<FUnrealMCPRecord>();
        TSet<FString> Names;
        for (const TSharedPtr<FUnrealMCPValue>& Value : *Records)
        {
            const TSharedPtr<FUnrealMCPRecord>* Record = nullptr;
            FString Section;
            constexpr int32 PrefixLength = 16;
            if (!Value.IsValid() || !Value->TryGetObject(Record)
                || Record == nullptr || !Record->IsValid()
                || !(*Record)->TryGetStringField(TEXT("section"), Section)
                || !Section.StartsWith(TEXT("gameplay_effect_")))
            {
                OutError = {TEXT("extension_contract_violation"),
                    TEXT("The GAS adapter returned an invalid Gameplay Effect block")};
                return false;
            }
            const FString Name = Section.RightChop(PrefixLength);
            if (Name.IsEmpty() || Names.Contains(Name))
            {
                OutError = {TEXT("extension_contract_violation"),
                    TEXT("The GAS adapter returned colliding Gameplay Effect subrecords")};
                return false;
            }
            Names.Add(Name);
            (*Record)->RemoveField(TEXT("section"));
            Block->SetObjectField(Name, *Record);
        }
        FString Fingerprint;
        if (!Handler.AppendFingerprint(
            *Effect, GameplayEffectOperation, Fingerprint, ExtensionError))
        {
            ConvertGameplayEffectInspectionError(ExtensionError, OutError);
            return false;
        }
        if (Context.Selector.IsRoot())
        {
            if (!Document.Add({TEXT("selectors"), TEXT("array"),
                MakeShared<FUnrealMCPValueArray>(TArray<TSharedPtr<FUnrealMCPValue>>{
                    MakeShared<FUnrealMCPValueString>(TEXT("gameplay_effect"))})}, OutError))
            {
                return false;
            }
        }
        else
        {
            const TSharedRef<FUnrealMCPRecord> Selection = MakeShared<FUnrealMCPRecord>();
            Selection->SetStringField(TEXT("selector"), TEXT("gameplay_effect"));
            Selection->SetStringField(TEXT("kind"), TEXT("record"));
            if (!Document.Add({TEXT("selection"), TEXT("record"),
                MakeShared<FUnrealMCPValueObject>(Selection)}, OutError))
            {
                return false;
            }
        }
        return Document.Add({TEXT("gameplay_effect"), TEXT("record"),
                MakeShared<FUnrealMCPValueObject>(Block)}, OutError)
            && Snapshot.Add(TEXT("gameplay_effect"), Fingerprint, OutError);
    }

private:
    FGameplayEffectInspectionHandler Handler;
};

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPGASGameplayEffectInspectionTest,
    "UnrealMCP.GAS.GameplayEffectInspection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPGASGameplayEffectInspectionTest::RunTest(const FString& Parameters)
{
    UPackage* Package = CreatePackage(TEXT("/Engine/Transient/UnrealMCPGASGameplayEffectInspectionTest"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        UGameplayEffect::StaticClass(), Package, TEXT("GE_InspectionTest"),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    TestNotNull(TEXT("Gameplay Effect Blueprint is created"), Blueprint);
    if (Blueprint == nullptr) return false;
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    UGameplayEffect* Defaults = Blueprint->GeneratedClass != nullptr
        ? Cast<UGameplayEffect>(Blueprint->GeneratedClass->GetDefaultObject(false)) : nullptr;
    TestNotNull(TEXT("Gameplay Effect defaults resolve"), Defaults);
    if (Defaults == nullptr) return false;

    Defaults->DurationPolicy = EGameplayEffectDurationType::HasDuration;
    Defaults->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(5.0f));
    Defaults->Period = FScalableFloat(1.0f);
    FGameplayModifierInfo& ScalableModifier = Defaults->Modifiers.AddDefaulted_GetRef();
    ScalableModifier.ModifierOp = EGameplayModOp::Additive;
    ScalableModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(10.0f));
    FGameplayModifierInfo& AttributeModifier = Defaults->Modifiers.AddDefaulted_GetRef();
    AttributeModifier.ModifierOp = EGameplayModOp::Multiplicitive;
    AttributeModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FAttributeBasedFloat());
    FGameplayModifierInfo& CustomModifier = Defaults->Modifiers.AddDefaulted_GetRef();
    CustomModifier.ModifierOp = EGameplayModOp::Division;
    CustomModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FCustomCalculationBasedFloat());
    FSetByCallerFloat SetByCaller;
    SetByCaller.DataName = TEXT("UnrealMCP.TestMagnitude");
    FGameplayModifierInfo& SetByCallerModifier = Defaults->Modifiers.AddDefaulted_GetRef();
    SetByCallerModifier.ModifierOp = EGameplayModOp::Override;
    SetByCallerModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
    UChanceToApplyGameplayEffectComponent& Chance =
        Defaults->AddComponent<UChanceToApplyGameplayEffectComponent>();
    Chance.SetChanceToApplyToTarget(FScalableFloat(0.75f));
    UAdditionalEffectsGameplayEffectComponent& Additional =
        Defaults->AddComponent<UAdditionalEffectsGameplayEffectComponent>();
    Additional.OnCompleteAlways.Add(Defaults->GetClass());

    const bool bDirtyBefore = Package->IsDirty();
    TArray<TSharedPtr<FUnrealMCPValue>> FirstRecords;
    FString FirstFingerprint;
    FUnrealMCPExtensionError Error;
    TestTrue(TEXT("Typed Gameplay Effect payload builds"),
        BuildGameplayEffectPayload(*Defaults, FirstRecords, FirstFingerprint, Error));
    TArray<TSharedPtr<FUnrealMCPValue>> SecondRecords;
    FString SecondFingerprint;
    TestTrue(TEXT("Repeated Gameplay Effect inspection succeeds"),
        BuildGameplayEffectPayload(*Defaults, SecondRecords, SecondFingerprint, Error));
    TestEqual(TEXT("All Gameplay Effect sections are returned"),
        FirstRecords.Num(), UnrealMCPGAS::MaxGameplayEffectInspectionRecords);
    TestEqual(TEXT("Gameplay Effect fingerprint is deterministic"),
        FirstFingerprint, SecondFingerprint);
    TestEqual(TEXT("Gameplay Effect inspection preserves package dirtiness"),
        Package->IsDirty(), bDirtyBefore);
    TestTrue(TEXT("Gameplay Effect fingerprint includes typed state"),
        !FirstFingerprint.IsEmpty());
    const TSharedPtr<FUnrealMCPRecord>* ModifierRecord = nullptr;
    const TSharedPtr<FUnrealMCPRecord>* RelationshipRecord = nullptr;
    for (const TSharedPtr<FUnrealMCPValue>& Value : FirstRecords)
    {
        const TSharedPtr<FUnrealMCPRecord>* Object = nullptr;
        FString Section;
        if (!Value.IsValid() || !Value->TryGetObject(Object) || Object == nullptr
            || !(*Object)->TryGetStringField(TEXT("section"), Section))
        {
            continue;
        }
        if (Section == TEXT("gameplay_effect_modifiers")) ModifierRecord = Object;
        if (Section == TEXT("gameplay_effect_relationships")) RelationshipRecord = Object;
    }
    TestNotNull(TEXT("Modifier section resolves"), ModifierRecord);
    if (ModifierRecord != nullptr)
    {
        const TArray<TSharedPtr<FUnrealMCPValue>>* Modifiers = nullptr;
        TestTrue(TEXT("Modifier records resolve"),
            (*ModifierRecord)->TryGetArrayField(TEXT("modifiers"), Modifiers) && Modifiers != nullptr);
        TSet<FString> MagnitudeTypes;
        if (Modifiers != nullptr)
        {
            for (const TSharedPtr<FUnrealMCPValue>& Value : *Modifiers)
            {
                const TSharedPtr<FUnrealMCPRecord>* Object = nullptr;
                const TSharedPtr<FUnrealMCPRecord>* Magnitude = nullptr;
                FString Type;
                if (Value.IsValid() && Value->TryGetObject(Object) && Object != nullptr
                    && (*Object)->TryGetObjectField(TEXT("magnitude"), Magnitude) && Magnitude != nullptr
                    && (*Magnitude)->TryGetStringField(TEXT("type"), Type))
                {
                    MagnitudeTypes.Add(Type);
                }
            }
        }
        for (const FString& Expected : {
            FString(TEXT("scalable_float")), FString(TEXT("attribute_based")),
            FString(TEXT("custom_calculation_class")), FString(TEXT("set_by_caller"))})
        {
            TestTrue(*FString::Printf(TEXT("Magnitude form %s is represented"), *Expected),
                MagnitudeTypes.Contains(Expected));
        }
    }
    TestNotNull(TEXT("Relationship section resolves"), RelationshipRecord);
    if (RelationshipRecord != nullptr)
    {
        bool bCycle = false;
        TestTrue(TEXT("Cyclic additional effects are reported"),
            (*RelationshipRecord)->TryGetBoolField(TEXT("chain_cycle"), bCycle) && bCycle);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPGASGameplayEffectLiveFixtureTest,
    "UnrealMCP.GAS.GameplayEffectLiveFixture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPGASGameplayEffectLiveFixtureTest::RunTest(const FString& Parameters)
{
    auto RemoveFixture = [](const TCHAR* PackageName)
    {
        const FString Filename = FPackageName::LongPackageNameToFilename(
            PackageName, FPackageName::GetAssetPackageExtension());
        return !IFileManager::Get().FileExists(*Filename)
            || IFileManager::Get().Delete(*Filename, false, true);
    };
    auto SaveBlueprint = [this](UBlueprint* Blueprint)
    {
        if (Blueprint == nullptr) return false;
        const FString Filename = FPackageName::LongPackageNameToFilename(
            Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        SaveArgs.bSlowTask = false;
        return UPackage::SavePackage(
            Blueprint->GetOutermost(), Blueprint, *Filename, SaveArgs);
    };

    TestTrue(TEXT("existing Gameplay Effect fixture is removed"),
        RemoveFixture(TEXT("/Game/UnrealMCPGAS/GE_InspectionFixture")));
    TestTrue(TEXT("existing Gameplay Ability fixture is removed"),
        RemoveFixture(TEXT("/Game/UnrealMCPGAS/GA_EffectReferenceFixture")));
    UPackage* EffectPackage = CreatePackage(TEXT("/Game/UnrealMCPGAS/GE_InspectionFixture"));
    UBlueprint* EffectBlueprint = FKismetEditorUtilities::CreateBlueprint(
        UGameplayEffect::StaticClass(), EffectPackage, TEXT("GE_InspectionFixture"),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    TestNotNull(TEXT("saved Gameplay Effect fixture is created"), EffectBlueprint);
    if (EffectBlueprint == nullptr) return false;
    FKismetEditorUtilities::CompileBlueprint(EffectBlueprint);
    UGameplayEffect* EffectDefaults = Cast<UGameplayEffect>(
        EffectBlueprint->GeneratedClass->GetDefaultObject(false));
    TestNotNull(TEXT("saved Gameplay Effect defaults resolve"), EffectDefaults);
    if (EffectDefaults == nullptr) return false;
    EffectDefaults->DurationPolicy = EGameplayEffectDurationType::HasDuration;
    EffectDefaults->DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(6.0f));
    EffectDefaults->Period = FScalableFloat(1.5f);
    FGameplayModifierInfo& Modifier = EffectDefaults->Modifiers.AddDefaulted_GetRef();
    Modifier.ModifierOp = EGameplayModOp::Additive;
    Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(4.0f));
    UChanceToApplyGameplayEffectComponent& Chance =
        EffectDefaults->AddComponent<UChanceToApplyGameplayEffectComponent>();
    Chance.SetChanceToApplyToTarget(FScalableFloat(0.8f));
    FKismetEditorUtilities::CompileBlueprint(EffectBlueprint);
    TestTrue(TEXT("saved Gameplay Effect fixture persists"), SaveBlueprint(EffectBlueprint));

    UPackage* AbilityPackage = CreatePackage(TEXT("/Game/UnrealMCPGAS/GA_EffectReferenceFixture"));
    UBlueprint* AbilityBlueprint = FKismetEditorUtilities::CreateBlueprint(
        UGameplayAbility::StaticClass(), AbilityPackage, TEXT("GA_EffectReferenceFixture"),
        BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    TestNotNull(TEXT("saved Gameplay Ability reference fixture is created"), AbilityBlueprint);
    if (AbilityBlueprint == nullptr) return false;
    FKismetEditorUtilities::CompileBlueprint(AbilityBlueprint);
    UGameplayAbility* AbilityDefaults = Cast<UGameplayAbility>(
        AbilityBlueprint->GeneratedClass->GetDefaultObject(false));
    TestNotNull(TEXT("saved Gameplay Ability defaults resolve"), AbilityDefaults);
    if (AbilityDefaults == nullptr) return false;
    FClassProperty* CostProperty = FindFProperty<FClassProperty>(
        AbilityDefaults->GetClass(), TEXT("CostGameplayEffectClass"));
    TestNotNull(TEXT("Gameplay Ability cost-effect property resolves"), CostProperty);
    if (CostProperty == nullptr) return false;
    CostProperty->SetObjectPropertyValue_InContainer(
        AbilityDefaults, EffectBlueprint->GeneratedClass);
    FKismetEditorUtilities::CompileBlueprint(AbilityBlueprint);
    TestTrue(TEXT("saved Gameplay Ability reference fixture persists"), SaveBlueprint(AbilityBlueprint));
    UE_LOG(LogTemp, Display, TEXT("UNREAL_MCP_GAS_EFFECT_FIXTURE=%s"), *EffectBlueprint->GetPathName());
    return true;
}
#endif
}

namespace UnrealMCPGAS
{
FUnrealMCPCompanionAssetFamily MakeGameplayEffectInspectionFamily()
{
    FUnrealMCPCompanionAssetFamily Family;
    Family.FamilyId = TEXT("gameplay_effect");
    Family.NativeClassPath = UGameplayEffect::StaticClass()->GetPathName();
    Family.ClassPolicy = EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived;
    Family.Priority = 200;
    Family.RequiredModules = {TEXT("GameplayAbilities")};
    Family.Bounds.MaxDocumentBytes = 4 * 1024 * 1024;
    Family.Bounds.MaxValueNodes = 65536;
    Family.Limits = {
        {TEXT("records"), MaxGameplayEffectInspectionRecords},
        {TEXT("modifiers"), MaxGameplayEffectModifiers},
        {TEXT("executions"), MaxGameplayEffectExecutions},
        {TEXT("cues"), MaxGameplayEffectCues},
        {TEXT("components"), MaxGameplayEffectComponents},
        {TEXT("granted_abilities"), MaxGameplayEffectGrantedAbilities},
        {TEXT("effect_references"), MaxGameplayEffectReferences},
        {TEXT("requirements"), MaxGameplayEffectRequirements},
        {TEXT("relationships"), MaxGameplayEffectRelationships},
        {TEXT("collection_scan"), MaxGameplayEffectCollectionScan},
        {TEXT("chain_depth"), MaxGameplayEffectChainDepth},
        {TEXT("chain_assets"), MaxGameplayEffectChainAssets}};
    Family.Capabilities.bInspection = true;
    Family.SelectorRoutes = {
        {TEXT("gameplay_effect"), {TEXT("gameplay_effect")}, false, false}};
    Family.InspectionAdapter = MakeShared<FGameplayEffectAssetInspectionAdapter>();
    Family.SnapshotBuilder = [](UObject* Asset)
    {
        UGameplayEffect* Effect = ResolveGameplayEffectDefaults(Asset);
        if (Effect == nullptr) return FString();
        FGameplayEffectInspectionHandler Handler;
        FUnrealMCPExtensionError Error;
        FString Fingerprint;
        return Handler.AppendFingerprint(
            *Effect, GameplayEffectOperation, Fingerprint, Error) ? Fingerprint : FString();
    };
    return Family;
}
}
