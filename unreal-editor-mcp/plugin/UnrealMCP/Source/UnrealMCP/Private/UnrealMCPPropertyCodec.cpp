#include "UnrealMCPPropertyCodec.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "UnrealMCPProtocol.h"
#include "UnrealMCPVersion.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace
{
const TSet<FName> SafeStructs = {
    TEXT("Vector"), TEXT("Vector2D"), TEXT("Vector4"), TEXT("Rotator"), TEXT("Quat"), TEXT("Transform"),
    TEXT("Color"), TEXT("LinearColor"), TEXT("IntPoint"), TEXT("IntVector"), TEXT("IntVector4")};

bool IsSafeReferencedObject(const UObject* Object)
{
    if (Object == nullptr) return true;
    if (Object->IsA<UClass>()) return !IsEditorOnlyObject(Object);
    if (Object->HasAnyFlags(RF_Transient) || IsEditorOnlyObject(Object)) return false;
    const UPackage* Package = Object->GetOutermost();
    return Object->IsAsset() && Package != nullptr && Package != GetTransientPackage() && !Package->HasAnyFlags(RF_Transient)
        && FPackageName::IsValidLongPackageName(Package->GetName(), true);
}

bool ReadString(const TSharedPtr<FJsonValue>& Value, FString& Out)
{
    return Value.IsValid() && Value->Type == EJson::String && Value->TryGetString(Out) && Out.Len() <= 4096;
}

bool ReadFiniteNumber(const TSharedPtr<FJsonValue>& Value, double& Out)
{
    return Value.IsValid() && Value->Type == EJson::Number && Value->TryGetNumber(Out) && FMath::IsFinite(Out);
}

bool IsFlagsEnum(const UEnum* Enum)
{
    return Enum != nullptr && (Enum->HasMetaData(TEXT("Bitflags")) || Enum->HasMetaData(TEXT("UseEnumValuesAsMaskValuesInEditor")));
}

int64 EnumValueByName(const UEnum* Enum, const FString& Name)
{
    if (Enum == nullptr) return INDEX_NONE;
    const int64 Direct = Enum->GetValueByNameString(Name);
    if (Direct != INDEX_NONE) return Direct;
    const int64 Qualified = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + Name);
    if (Qualified != INDEX_NONE) return Qualified;
    for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
    {
        if (Enum->GetNameStringByIndex(Index).Equals(Name, ESearchCase::CaseSensitive))
        {
            return Enum->GetValueByIndex(Index);
        }
    }
    return INDEX_NONE;
}

UEnum* PropertyEnum(FProperty* Property)
{
    if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property)) return EnumProperty->GetEnum();
    if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property); ByteProperty != nullptr && ByteProperty->Enum != nullptr)
    {
        return ByteProperty->Enum;
    }
    if (Property != nullptr && Property->HasMetaData(TEXT("Bitmask")))
    {
        const FString EnumPath = Property->GetMetaData(TEXT("BitmaskEnum"));
        if (!EnumPath.IsEmpty())
        {
            if (UEnum* Enum = UClass::TryFindTypeSlow<UEnum>(EnumPath, EFindFirstObjectOptions::ExactClass))
            {
                return Enum;
            }
            return LoadObject<UEnum>(nullptr, *EnumPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
        }
    }
    return nullptr;
}

bool IsReadableStructType(const FStructProperty* Property, const TCHAR* Path)
{
    return Property != nullptr && Property->Struct != nullptr && Property->Struct->GetPathName() == Path;
}

bool IsReadable(const FProperty* Property, FString& OutKind, int32 Depth, bool bRequireEditable)
{
    if (Property == nullptr || Depth > UnrealMCP::MaxGameDataDepth || Property->ArrayDim != 1
        || (bRequireEditable && !Property->HasAnyPropertyFlags(CPF_Edit))
        || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DisableEditOnTemplate | CPF_Deprecated | CPF_EditorOnly
            | CPF_InstancedReference | CPF_ContainsInstancedReference | CPF_ExportObject)
        || Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>()
        || Property->IsA<FDelegateProperty>() || Property->IsA<FMulticastDelegateProperty>() || Property->IsA<FInterfaceProperty>())
    {
        return false;
    }
    if (Property->IsA<FBoolProperty>()) OutKind = TEXT("bool");
    else if (Property->IsA<FNumericProperty>() && PropertyEnum(const_cast<FProperty*>(Property)) == nullptr) OutKind = TEXT("number");
    else if (Property->IsA<FNameProperty>()) OutKind = TEXT("name");
    else if (Property->IsA<FStrProperty>()) OutKind = TEXT("string");
    else if (Property->IsA<FTextProperty>()) OutKind = TEXT("text");
    else if (PropertyEnum(const_cast<FProperty*>(Property)) != nullptr) OutKind = TEXT("enum");
    else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        if (IsReadableStructType(Struct, TEXT("/Script/CoreUObject.Guid"))) OutKind = TEXT("guid");
        else if (IsReadableStructType(Struct, TEXT("/Script/GameplayTags.GameplayTag"))) OutKind = TEXT("gameplay_tag");
        else if (IsReadableStructType(Struct, TEXT("/Script/GameplayTags.GameplayTagContainer"))) OutKind = TEXT("gameplay_tag_container");
        else
        {
            int32 Fields = 0;
            for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
            {
                if (It->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly)) continue;
                if (++Fields > UnrealMCP::MaxGameDataFields) return false;
                FString ChildKind;
                if (!IsReadable(*It, ChildKind, Depth + 1, false)) return false;
            }
            OutKind = TEXT("struct");
        }
    }
    else if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        FString ChildKind;
        if (!IsReadable(Array->Inner, ChildKind, Depth + 1, false)) return false;
        OutKind = TEXT("array");
    }
    else if (Property->IsA<FClassProperty>() || Property->IsA<FSoftClassProperty>()) OutKind = TEXT("class_reference");
    else if (Property->IsA<FObjectPropertyBase>() || Property->IsA<FSoftObjectProperty>()) OutKind = TEXT("object_reference");
    else return false;
    return true;
}

bool EncodeReadableGameplayTag(const UScriptStruct* Struct, const void* Address, FString& Out)
{
    const FNameProperty* TagName = Struct != nullptr
        ? CastField<FNameProperty>(Struct->FindPropertyByName(TEXT("TagName"))) : nullptr;
    if (TagName == nullptr || Address == nullptr) return false;
    Out = TagName->GetPropertyValue(TagName->ContainerPtrToValuePtr<void>(Address)).ToString();
    return Out.Len() <= 128;
}

bool EncodeValueAt(UObject* Object, FProperty* Property, const void* Address, int32 Depth, TSharedPtr<FJsonValue>& Out)
{
    if (Address == nullptr || Depth > UnrealMCP::MaxGameDataDepth) return false;
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property)) { Out = MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Address)); return true; }
    if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property); Numeric != nullptr && PropertyEnum(Property) == nullptr)
    {
        Out = MakeShared<FJsonValueNumber>(Numeric->IsFloatingPoint()
            ? Numeric->GetFloatingPointPropertyValue(Address)
            : static_cast<double>(Numeric->GetSignedIntPropertyValue(Address)));
        return true;
    }
    if (const FNameProperty* Name = CastField<FNameProperty>(Property)) { Out = MakeShared<FJsonValueString>(Name->GetPropertyValue(Address).ToString()); return true; }
    if (const FStrProperty* String = CastField<FStrProperty>(Property)) { Out = MakeShared<FJsonValueString>(String->GetPropertyValue(Address).Left(UnrealMCP::MaxStringLength)); return true; }
    if (const FTextProperty* Text = CastField<FTextProperty>(Property)) { Out = MakeShared<FJsonValueString>(Text->GetPropertyValue(Address).ToString().Left(UnrealMCP::MaxStringLength)); return true; }
    if (UEnum* Enum = PropertyEnum(Property))
    {
        if (IsFlagsEnum(Enum))
        {
            int64 NumericValue = 0;
            if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
                NumericValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Address);
            else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
                NumericValue = ByteProperty->GetPropertyValue(Address);
            else if (const FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
                NumericValue = NumericProperty->GetSignedIntPropertyValue(Address);
            TArray<TSharedPtr<FJsonValue>> Names;
            for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
            {
                const int64 Flag = Enum->GetValueByIndex(Index);
                if (Flag != 0 && !Enum->HasMetaData(TEXT("Hidden"), Index) && !Enum->HasMetaData(TEXT("Spacer"), Index)
                    && (NumericValue & Flag) == Flag)
                {
                    Names.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(Index)));
                }
            }
            Out = MakeShared<FJsonValueArray>(Names);
            return true;
        }
        int64 NumericValue = 0;
        if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
            NumericValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Address);
        else NumericValue = CastFieldChecked<FByteProperty>(Property)->GetPropertyValue(Address);
        const FString Name = Enum->GetNameStringByValue(NumericValue);
        if (Name.IsEmpty()) return false;
        Out = MakeShared<FJsonValueString>(Name);
        return true;
    }
    if (const FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(Property))
    {
        Out = MakeShared<FJsonValueString>(SoftObject->GetPropertyValue(Address).ToSoftObjectPath().ToString());
        return true;
    }
    if (const FClassProperty* Class = CastField<FClassProperty>(Property))
    {
        const UClass* Value = Cast<UClass>(Class->GetObjectPropertyValue(Address));
        if (!IsSafeReferencedObject(Value)) return false;
        Out = MakeShared<FJsonValueString>(Value != nullptr ? Value->GetPathName() : FString());
        return true;
    }
    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        const UObject* Value = ObjectProperty->GetObjectPropertyValue(Address);
        if (!IsSafeReferencedObject(Value)) return false;
        Out = MakeShared<FJsonValueString>(Value != nullptr ? Value->GetPathName() : FString());
        return true;
    }
    if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        if (IsReadableStructType(Struct, TEXT("/Script/CoreUObject.Guid")))
        {
            Out = MakeShared<FJsonValueString>(static_cast<const FGuid*>(Address)->ToString(EGuidFormats::Digits).ToLower());
            return true;
        }
        if (IsReadableStructType(Struct, TEXT("/Script/GameplayTags.GameplayTag")))
        {
            FString Name;
            if (!EncodeReadableGameplayTag(Struct->Struct, Address, Name)) return false;
            Out = MakeShared<FJsonValueString>(Name);
            return true;
        }
        if (IsReadableStructType(Struct, TEXT("/Script/GameplayTags.GameplayTagContainer")))
        {
            const FArrayProperty* Tags = CastField<FArrayProperty>(Struct->Struct->FindPropertyByName(TEXT("GameplayTags")));
            const FStructProperty* Tag = Tags != nullptr ? CastField<FStructProperty>(Tags->Inner) : nullptr;
            if (Tags == nullptr || Tag == nullptr) return false;
            FScriptArrayHelper Helper(Tags, Tags->ContainerPtrToValuePtr<void>(Address));
            if (Helper.Num() > UnrealMCP::MaxGameDataCollectionItems) return false;
            TArray<FString> Names;
            for (int32 Index = 0; Index < Helper.Num(); ++Index)
            {
                FString Name;
                if (!EncodeReadableGameplayTag(Tag->Struct, Helper.GetRawPtr(Index), Name)) return false;
                Names.Add(Name);
            }
            Names.Sort();
            TArray<TSharedPtr<FJsonValue>> Values;
            for (const FString& Name : Names) Values.Add(MakeShared<FJsonValueString>(Name));
            Out = MakeShared<FJsonValueArray>(Values);
            return true;
        }
        if (SafeStructs.Contains(Struct->Struct->GetFName()))
        {
            FString Exported;
            if (!Property->ExportText_Direct(Exported, Address, nullptr, Object, PPF_None)) return false;
            Out = MakeShared<FJsonValueString>(Exported.Left(UnrealMCP::MaxStringLength));
            return true;
        }
        const TSharedRef<FJsonObject> Fields = MakeShared<FJsonObject>();
        for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
        {
            if (It->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly)) continue;
            TSharedPtr<FJsonValue> Child;
            if (!EncodeValueAt(Object, *It, It->ContainerPtrToValuePtr<void>(Address), Depth + 1, Child)) return false;
            Fields->SetField(Struct->Struct->GetAuthoredNameForField(*It), Child);
        }
        const TSharedRef<FJsonObject> Tagged = MakeShared<FJsonObject>();
        Tagged->SetStringField(TEXT("kind"), TEXT("struct"));
        Tagged->SetObjectField(TEXT("fields"), Fields);
        Out = MakeShared<FJsonValueObject>(Tagged);
        return true;
    }
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Helper(Array, Address);
        if (Helper.Num() > UnrealMCP::MaxGameDataCollectionItems) return false;
        TArray<TSharedPtr<FJsonValue>> Values;
        for (int32 Index = 0; Index < Helper.Num(); ++Index)
        {
            TSharedPtr<FJsonValue> Child;
            if (!EncodeValueAt(Object, Array->Inner, Helper.GetRawPtr(Index), Depth + 1, Child)) return false;
            Values.Add(Child);
        }
        Out = MakeShared<FJsonValueArray>(Values);
        return true;
    }
    return false;
}

bool ImportReference(UObject* Object, FProperty* Property, const FString& Path, FUnrealMCPError& OutError)
{
    void* Address = Property->ContainerPtrToValuePtr<void>(Object);
    if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
    {
        UClass* Class = Path.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if ((Class == nullptr && !Path.IsEmpty()) || (Class != nullptr && !Class->IsChildOf(ClassProperty->MetaClass)) || !IsSafeReferencedObject(Class))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The class reference is missing, incompatible, transient, or editor-only")};
            return false;
        }
        ClassProperty->SetObjectPropertyValue(Address, Class);
        return true;
    }
    if (CastField<FSoftClassProperty>(Property) != nullptr || CastField<FSoftObjectProperty>(Property) != nullptr)
    {
        const FSoftClassProperty* SoftClass = CastField<FSoftClassProperty>(Property);
        const FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(Property);
        UObject* Value = nullptr;
        FSoftObjectPath SoftPath;
        if (SoftClass != nullptr)
        {
            SoftPath = FSoftClassPath(Path);
            Value = Path.IsEmpty() ? nullptr : FSoftClassPath(Path).TryLoadClass<UObject>();
        }
        else
        {
            SoftPath = FSoftObjectPath(Path);
            Value = Path.IsEmpty() ? nullptr : SoftPath.TryLoad();
        }
        const UClass* LoadedClass = Cast<UClass>(Value);
        const bool bIncompatibleClass = SoftClass != nullptr && Value != nullptr
            && (LoadedClass == nullptr || !LoadedClass->IsChildOf(SoftClass->MetaClass));
        const bool bIncompatibleObject = SoftClass == nullptr && SoftObject != nullptr && Value != nullptr && !Value->IsA(SoftObject->PropertyClass);
        if ((!Path.IsEmpty() && (!SoftPath.IsValid() || Value == nullptr)) || bIncompatibleClass || bIncompatibleObject
            || !IsSafeReferencedObject(Value))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The soft reference is missing, incompatible, transient, editor-only, or not packageable")};
            return false;
        }
        CastFieldChecked<FSoftObjectProperty>(Property)->SetPropertyValue(Address, FSoftObjectPtr(SoftPath));
        return true;
    }
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        UObject* Value = Path.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if ((Value == nullptr && !Path.IsEmpty()) || (Value != nullptr && !Value->IsA(ObjectProperty->PropertyClass)) || !IsSafeReferencedObject(Value))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The object reference is missing, incompatible, transient, editor-only, or not a packageable asset")};
            return false;
        }
        ObjectProperty->SetObjectPropertyValue(Address, Value);
        return true;
    }
    OutError = {TEXT("unsupported_property"), TEXT("The reference property is unsupported")};
    return false;
}
}

bool UnrealMCP::PropertyCodec::IsIdenticalToArchetype(const UObject* Object, const FProperty* Property)
{
    if (Object == nullptr || Property == nullptr)
    {
        return false;
    }
    const UObject* Archetype = Object->GetArchetype();
    if (Archetype == nullptr)
    {
        return false;
    }
    const void* DefaultValue = Property->ContainerPtrToValuePtrForDefaults<void>(
        Archetype->GetClass(), Archetype);
    return Property->Identical(Property->ContainerPtrToValuePtr<void>(Object), DefaultValue, PPF_None);
}

bool UnrealMCP::PropertyCodec::ExportValueText(
    const UObject* Object,
    const FProperty* Property,
    FString& OutText)
{
    if (Object == nullptr || Property == nullptr)
    {
        return false;
    }
    const UObject* Archetype = Object->GetArchetype();
    const void* DefaultValue = Archetype != nullptr
        ? Property->ContainerPtrToValuePtrForDefaults<void>(Archetype->GetClass(), Archetype)
        : nullptr;
    return Property->ExportText_Direct(
        OutText,
        Property->ContainerPtrToValuePtr<void>(Object),
        DefaultValue,
        const_cast<UObject*>(Object),
        PPF_None);
}

bool UnrealMCP::PropertyCodec::IsSupportedEditable(const FProperty* Property, FString& OutKind)
{
    if (Property == nullptr || Property->ArrayDim != 1 || !Property->HasAnyPropertyFlags(CPF_Edit)
        || Property->HasAnyPropertyFlags(CPF_Transient | CPF_DisableEditOnTemplate | CPF_Deprecated)
        || Property->IsA<FArrayProperty>() || Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>()
        || Property->IsA<FDelegateProperty>() || Property->IsA<FMulticastDelegateProperty>() || Property->IsA<FInterfaceProperty>())
    {
        return false;
    }
    if (Property->IsA<FBoolProperty>()) OutKind = TEXT("bool");
    else if (Property->IsA<FNumericProperty>() && PropertyEnum(const_cast<FProperty*>(Property)) == nullptr) OutKind = TEXT("number");
    else if (Property->IsA<FNameProperty>()) OutKind = TEXT("name");
    else if (Property->IsA<FStrProperty>()) OutKind = TEXT("string");
    else if (Property->IsA<FTextProperty>()) OutKind = TEXT("text");
    else if (PropertyEnum(const_cast<FProperty*>(Property)) != nullptr) OutKind = TEXT("enum");
    else if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        if (!SafeStructs.Contains(Struct->Struct->GetFName())) return false;
        OutKind = TEXT("struct");
    }
    else if (Property->IsA<FClassProperty>() || Property->IsA<FSoftClassProperty>()) OutKind = TEXT("class_reference");
    else if (Property->IsA<FObjectPropertyBase>() || Property->IsA<FSoftObjectProperty>()) OutKind = TEXT("object_reference");
    else return false;
    return true;
}

bool UnrealMCP::PropertyCodec::IsSupportedReadable(const FProperty* Property, FString& OutKind)
{
    return IsReadable(Property, OutKind, 0, true);
}

TSharedRef<FJsonObject> UnrealMCP::PropertyCodec::Encode(UObject* Object, FProperty* Property)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), Property != nullptr ? Property->GetName() : FString());
    FString Kind;
    const bool bSupported = Object != nullptr && IsSupportedReadable(Property, Kind);
    Result->SetBoolField(TEXT("supported"), bSupported);
    Result->SetStringField(TEXT("type"), bSupported ? Kind : TEXT("unsupported"));
    if (bSupported)
    {
        TSharedPtr<FJsonValue> Value;
        if (EncodeValueAt(Object, Property, Property->ContainerPtrToValuePtr<void>(Object), 0, Value))
            Result->SetField(TEXT("value"), Value);
        else
        {
            Result->SetBoolField(TEXT("supported"), false);
            Result->SetStringField(TEXT("type"), TEXT("unsupported"));
        }
    }
    return Result;
}

bool UnrealMCP::PropertyCodec::Set(
    UObject* Object,
    const FString& PropertyName,
    const TSharedPtr<FJsonValue>& Value,
    TSharedPtr<FJsonObject>& OutChanged,
    FUnrealMCPError& OutError)
{
    if (Object == nullptr || PropertyName.IsEmpty() || PropertyName.Len() > 128 || PropertyName.Contains(TEXT(".")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("property_name must identify one exact reflected property")};
        return false;
    }
    FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
    FString Kind;
    if (!IsSupportedEditable(Property, Kind))
    {
        OutError = {TEXT("unsupported_property"), TEXT("The property is missing or does not have safe editable default semantics")};
        return false;
    }
    FString Text;
    bool bImported = false;
    void* Address = Property->ContainerPtrToValuePtr<void>(Object);
    if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
    {
        bool Parsed = false;
        bImported = Value.IsValid() && Value->Type == EJson::Boolean && Value->TryGetBool(Parsed);
        if (bImported) Bool->SetPropertyValue(Address, Parsed);
    }
    else if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property); Numeric != nullptr && PropertyEnum(Property) == nullptr)
    {
        double Number = 0.0;
        bImported = ReadFiniteNumber(Value, Number);
        if (bImported && Numeric->IsInteger())
        {
            bImported = FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)) && FMath::Abs(Number) <= 9007199254740991.0;
            if (bImported) Numeric->SetIntPropertyValue(Address, static_cast<int64>(Number));
        }
        else if (bImported) Numeric->SetFloatingPointPropertyValue(Address, Number);
    }
    else if (FNameProperty* Name = CastField<FNameProperty>(Property))
    {
        bImported = ReadString(Value, Text);
        if (bImported) Name->SetPropertyValue(Address, FName(*Text));
    }
    else if (FStrProperty* String = CastField<FStrProperty>(Property))
    {
        bImported = ReadString(Value, Text);
        if (bImported) String->SetPropertyValue(Address, Text);
    }
    else if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
    {
        bImported = ReadString(Value, Text);
        if (bImported) TextProperty->SetPropertyValue(Address, FText::FromString(Text));
    }
    else if (UEnum* Enum = PropertyEnum(Property))
    {
        int64 EnumValue = 0;
        if (IsFlagsEnum(Enum) && Value.IsValid() && Value->Type == EJson::Array)
        {
            bImported = Value->AsArray().Num() <= 64;
            for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
            {
                FString FlagName;
                const int64 Flag = Item.IsValid() && Item->TryGetString(FlagName) ? EnumValueByName(Enum, FlagName) : INDEX_NONE;
                if (Flag == INDEX_NONE) { bImported = false; break; }
                EnumValue |= Flag;
            }
        }
        else
        {
            bImported = ReadString(Value, Text);
            EnumValue = bImported ? EnumValueByName(Enum, Text) : INDEX_NONE;
            bImported = bImported && EnumValue != INDEX_NONE;
        }
        if (bImported)
        {
            if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property)) EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(Address, EnumValue);
            else if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property)) ByteProperty->SetPropertyValue(Address, static_cast<uint8>(EnumValue));
            else CastFieldChecked<FNumericProperty>(Property)->SetIntPropertyValue(Address, EnumValue);
        }
    }
    else if (Kind == TEXT("class_reference") || Kind == TEXT("object_reference"))
    {
        bImported = ReadString(Value, Text) && ImportReference(Object, Property, Text, OutError);
        if (!bImported && !OutError.Code.IsEmpty()) return false;
    }
    else
    {
        bImported = ReadString(Value, Text) && Property->ImportText_Direct(*Text, Address, Object, PPF_None) != nullptr;
    }
    if (!bImported)
    {
        OutError = {TEXT("invalid_argument"), TEXT("The property value does not match the supported property form")};
        return false;
    }
    // Capture the assigned value before notification: some editor handlers reconstruct and
    // invalidate component templates. The owning mutator re-resolves its live target afterward.
    OutChanged = Encode(Object, Property);
    FPropertyChangedEvent ChangedEvent(Property, EPropertyChangeType::ValueSet);
    Object->PostEditChangeProperty(ChangedEvent);
    return true;
}
