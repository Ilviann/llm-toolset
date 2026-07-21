#include "UnrealMCPPropertyCodec.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/PackageName.h"
#include "UnrealMCPProtocol.h"
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

TSharedPtr<FJsonValue> EncodeValue(UObject* Object, FProperty* Property, const FString& Kind)
{
    const void* Address = Property->ContainerPtrToValuePtr<void>(Object);
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property)) return MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Address));
    if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property); Numeric != nullptr && PropertyEnum(Property) == nullptr)
    {
        return MakeShared<FJsonValueNumber>(Numeric->IsFloatingPoint()
            ? Numeric->GetFloatingPointPropertyValue(Address)
            : static_cast<double>(Numeric->GetSignedIntPropertyValue(Address)));
    }
    if (const FNameProperty* Name = CastField<FNameProperty>(Property)) return MakeShared<FJsonValueString>(Name->GetPropertyValue(Address).ToString());
    if (const FStrProperty* String = CastField<FStrProperty>(Property)) return MakeShared<FJsonValueString>(String->GetPropertyValue(Address));
    if (const FTextProperty* Text = CastField<FTextProperty>(Property)) return MakeShared<FJsonValueString>(Text->GetPropertyValue(Address).ToString());
    if (UEnum* Enum = PropertyEnum(Property))
    {
        FString Exported;
        Property->ExportText_InContainer(0, Exported, Object, Object->GetArchetype(), Object, PPF_None);
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
            return MakeShared<FJsonValueArray>(Names);
        }
        return MakeShared<FJsonValueString>(Exported);
    }
    if (const FSoftObjectProperty* SoftObject = CastField<FSoftObjectProperty>(Property))
    {
        return MakeShared<FJsonValueString>(SoftObject->GetPropertyValue(Address).ToSoftObjectPath().ToString());
    }
    if (const FClassProperty* Class = CastField<FClassProperty>(Property))
    {
        const UClass* Value = Cast<UClass>(Class->GetObjectPropertyValue(Address));
        return MakeShared<FJsonValueString>(Value != nullptr ? Value->GetPathName() : FString());
    }
    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        const UObject* Value = ObjectProperty->GetObjectPropertyValue(Address);
        return MakeShared<FJsonValueString>(Value != nullptr ? Value->GetPathName() : FString());
    }
    FString Exported;
    Property->ExportText_InContainer(0, Exported, Object, Object->GetArchetype(), Object, PPF_None);
    return MakeShared<FJsonValueString>(Exported.Left(4096));
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

TSharedRef<FJsonObject> UnrealMCP::PropertyCodec::Encode(UObject* Object, FProperty* Property)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), Property != nullptr ? Property->GetName() : FString());
    FString Kind;
    const bool bSupported = Object != nullptr && IsSupportedEditable(Property, Kind);
    Result->SetBoolField(TEXT("supported"), bSupported);
    Result->SetStringField(TEXT("type"), bSupported ? Kind : TEXT("unsupported"));
    if (bSupported) Result->SetField(TEXT("value"), EncodeValue(Object, Property, Kind));
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
