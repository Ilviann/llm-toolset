#include "UnrealMCPGameDataValueCodec.h"

#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMCPK2TypeCodec.h"
#include "UnrealMCPProtocol.h"
#include "UnrealMCPVersion.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace
{
bool CodecHasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed)
{
    TSet<FString> Names;
    for (const TCHAR* Name : Allowed) Names.Add(Name);
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
    {
        if (!Names.Contains(Pair.Key)) return false;
    }
    return true;
}

FString CanonicalJson(const TSharedPtr<FJsonValue>& Value)
{
    FString Result;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    FJsonSerializer::Serialize(Value, FString(), Writer);
    return Result;
}

bool FailDepth(int32 Depth, FUnrealMCPError& OutError)
{
    if (Depth <= UnrealMCP::MaxGameDataDepth) return false;
    OutError = {TEXT("data_limit_exceeded"), TEXT("Nested game-data values exceed the configured depth limit")};
    return true;
}

UEnum* PropertyEnum(const FProperty* Property)
{
    if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property)) return EnumProperty->GetEnum();
    if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property)) return ByteProperty->Enum;
    return nullptr;
}

int64 EnumValue(const UEnum* Enum, const FString& Name)
{
    if (Enum == nullptr) return INDEX_NONE;
    int64 Value = Enum->GetValueByNameString(Name);
    if (Value != INDEX_NONE) return Value;
    Value = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + Name);
    return Value;
}

bool IsUnsafe(const FProperty* Property)
{
    return Property == nullptr || Property->ArrayDim != 1
        || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_InstancedReference | CPF_ContainsInstancedReference)
        || Property->IsA<FDelegateProperty>() || Property->IsA<FMulticastDelegateProperty>() || Property->IsA<FInterfaceProperty>();
}

bool SafeReference(const UObject* Object)
{
    if (Object == nullptr) return true;
    if (Object->HasAnyFlags(RF_Transient) || IsEditorOnlyObject(Object)) return false;
    if (Object->IsA<UClass>()) return true;
    const UPackage* Package = Object->GetOutermost();
    return Object->IsAsset() && Package != nullptr && Package != GetTransientPackage()
        && FPackageName::IsValidLongPackageName(Package->GetName(), true);
}

TSharedRef<FJsonObject> ReferenceValue(const FString& Path)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("reference"));
    Result->SetStringField(TEXT("path"), Path);
    return Result;
}

bool ReadReference(const TSharedPtr<FJsonValue>& Input, FString& OutPath, FUnrealMCPError& OutError)
{
    const TSharedPtr<FJsonObject>* Object = nullptr; FString Kind;
    if (!Input.IsValid() || !Input->TryGetObject(Object) || Object == nullptr || !(*Object).IsValid()
        || !CodecHasOnlyFields(**Object, {TEXT("kind"), TEXT("path")})
        || !(*Object)->TryGetStringField(TEXT("kind"), Kind) || Kind != TEXT("reference")
        || !(*Object)->TryGetStringField(TEXT("path"), OutPath) || OutPath.Len() > 512
        || OutPath.Contains(TEXT("..")) || OutPath.Contains(TEXT("\\"))
        || (!OutPath.IsEmpty() && !OutPath.StartsWith(TEXT("/"))))
    {
        OutError = {TEXT("invalid_row"), TEXT("Object and class fields require one bounded tagged reference")};
        return false;
    }
    return true;
}

FProperty* FindAuthoredProperty(const UScriptStruct* Struct, const FString& Name)
{
    if (Struct == nullptr || Name.IsEmpty() || Name.Len() > 128) return nullptr;
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        if (It->GetName() == Name || Struct->GetAuthoredNameForField(*It) == Name) return *It;
    }
    return nullptr;
}
}

TSharedRef<FJsonObject> UnrealMCP::GameDataValueCodec::EncodeType(const FProperty* Property)
{
    FEdGraphPinType PinType;
    if (Property != nullptr && GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PinType))
    {
        return UnrealMCP::K2TypeCodec::EncodeType(PinType);
    }
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("category"), TEXT("unsupported"));
    Result->SetStringField(TEXT("container"), TEXT("none"));
    Result->SetBoolField(TEXT("reference"), false);
    Result->SetBoolField(TEXT("const"), false);
    Result->SetBoolField(TEXT("supported"), false);
    return Result;
}

bool UnrealMCP::GameDataValueCodec::Encode(
    const FProperty* Property, const void* Value, int32 Depth,
    TSharedPtr<FJsonValue>& OutValue, FUnrealMCPError& OutError)
{
    if (FailDepth(Depth, OutError) || IsUnsafe(Property) || Value == nullptr)
    {
        if (OutError.Code.IsEmpty()) OutError = {TEXT("unsupported_type"), TEXT("The row field type is not supported")};
        return false;
    }
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
    {
        OutValue = MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Value));
        return true;
    }
    if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property); Numeric != nullptr && PropertyEnum(Property) == nullptr)
    {
        double Number = 0.0;
        if (Numeric->IsFloatingPoint()) Number = Numeric->GetFloatingPointPropertyValue(Value);
        else if (!LexTryParseString(Number, *Numeric->GetNumericPropertyValueToString(Value)))
        { OutError = {TEXT("unsupported_type"), TEXT("The numeric row value could not be encoded")}; return false; }
        if (!FMath::IsFinite(Number) || (Numeric->IsInteger() && FMath::Abs(Number) > 9007199254740991.0))
        {
            OutError = {TEXT("unsupported_type"), TEXT("The numeric row value cannot be represented exactly as bounded JSON")};
            return false;
        }
        OutValue = MakeShared<FJsonValueNumber>(Number);
        return true;
    }
    if (const UEnum* Enum = PropertyEnum(Property))
    {
        int64 Numeric = 0;
        if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
            Numeric = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(Value);
        else Numeric = CastFieldChecked<FByteProperty>(Property)->GetPropertyValue(Value);
        const FString Name = Enum->GetNameStringByValue(Numeric);
        if (Name.IsEmpty())
        {
            OutError = {TEXT("invalid_row"), TEXT("The enum row value is not one live enumerator")};
            return false;
        }
        OutValue = MakeShared<FJsonValueString>(Name);
        return true;
    }
    if (const FNameProperty* Name = CastField<FNameProperty>(Property))
    {
        OutValue = MakeShared<FJsonValueString>(Name->GetPropertyValue(Value).ToString()); return true;
    }
    if (const FStrProperty* String = CastField<FStrProperty>(Property))
    {
        OutValue = MakeShared<FJsonValueString>(String->GetPropertyValue(Value).Left(UnrealMCP::MaxStringLength)); return true;
    }
    if (const FTextProperty* Text = CastField<FTextProperty>(Property))
    {
        OutValue = MakeShared<FJsonValueString>(Text->GetPropertyValue(Value).ToString().Left(UnrealMCP::MaxStringLength)); return true;
    }
    if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
    {
        const UClass* Class = Cast<UClass>(ClassProperty->GetObjectPropertyValue(Value));
        if (!SafeReference(Class)) { OutError = {TEXT("unsupported_type"), TEXT("The row contains an unsafe class reference")}; return false; }
        OutValue = MakeShared<FJsonValueObject>(ReferenceValue(Class != nullptr ? Class->GetPathName() : FString())); return true;
    }
    if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Property))
    {
        const FString Path = Soft->GetPropertyValue(Value).ToSoftObjectPath().ToString();
        if (Path.Len() > 512 || Path.Contains(TEXT("..")) || Path.Contains(TEXT("\\")) || (!Path.IsEmpty() && !Path.StartsWith(TEXT("/"))))
        { OutError = {TEXT("unsupported_type"), TEXT("The row contains an unsafe soft reference")}; return false; }
        OutValue = MakeShared<FJsonValueObject>(ReferenceValue(Path)); return true;
    }
    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        const UObject* Object = ObjectProperty->GetObjectPropertyValue(Value);
        if (!SafeReference(Object)) { OutError = {TEXT("unsupported_type"), TEXT("The row contains an unsafe object graph")}; return false; }
        OutValue = MakeShared<FJsonValueObject>(ReferenceValue(Object != nullptr ? Object->GetPathName() : FString())); return true;
    }
    if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        bool bSucceeded = false;
        const TSharedRef<FJsonObject> Fields = EncodeFields(StructProperty->Struct, Value, Depth + 1, OutError, bSucceeded);
        if (!bSucceeded) return false;
        const TSharedRef<FJsonObject> Tagged = MakeShared<FJsonObject>();
        Tagged->SetStringField(TEXT("kind"), TEXT("struct"));
        Tagged->SetObjectField(TEXT("fields"), Fields);
        OutValue = MakeShared<FJsonValueObject>(Tagged); return true;
    }
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Helper(Array, Value);
        if (Helper.Num() > UnrealMCP::MaxGameDataCollectionItems) { OutError = {TEXT("data_limit_exceeded"), TEXT("A row array exceeds the collection limit")}; return false; }
        TArray<TSharedPtr<FJsonValue>> Items;
        for (int32 Index = 0; Index < Helper.Num(); ++Index)
        {
            TSharedPtr<FJsonValue> Item;
            if (!Encode(Array->Inner, Helper.GetRawPtr(Index), Depth + 1, Item, OutError)) return false;
            Items.Add(Item);
        }
        OutValue = MakeShared<FJsonValueArray>(Items); return true;
    }
    if (const FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        FScriptSetHelper Helper(Set, Value);
        if (Helper.Num() > UnrealMCP::MaxGameDataCollectionItems) { OutError = {TEXT("data_limit_exceeded"), TEXT("A row set exceeds the collection limit")}; return false; }
        TArray<TSharedPtr<FJsonValue>> Items;
        for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index) if (Helper.IsValidIndex(Index))
        {
            TSharedPtr<FJsonValue> Item;
            if (!Encode(Set->ElementProp, Helper.GetElementPtr(Index), Depth + 1, Item, OutError)) return false;
            Items.Add(Item);
        }
        Items.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
        { return CanonicalJson(Left) < CanonicalJson(Right); });
        const TSharedRef<FJsonObject> Tagged = MakeShared<FJsonObject>(); Tagged->SetStringField(TEXT("kind"), TEXT("set")); Tagged->SetArrayField(TEXT("items"), Items);
        OutValue = MakeShared<FJsonValueObject>(Tagged); return true;
    }
    if (const FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper Helper(Map, Value);
        if (Helper.Num() > UnrealMCP::MaxGameDataCollectionItems) { OutError = {TEXT("data_limit_exceeded"), TEXT("A row map exceeds the collection limit")}; return false; }
        TArray<TSharedPtr<FJsonValue>> Entries;
        for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index) if (Helper.IsValidIndex(Index))
        {
            TSharedPtr<FJsonValue> Key; TSharedPtr<FJsonValue> Item;
            if (!Encode(Map->KeyProp, Helper.GetKeyPtr(Index), Depth + 1, Key, OutError)
                || !Encode(Map->ValueProp, Helper.GetValuePtr(Index), Depth + 1, Item, OutError)) return false;
            const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>(); Entry->SetField(TEXT("key"), Key); Entry->SetField(TEXT("value"), Item);
            Entries.Add(MakeShared<FJsonValueObject>(Entry));
        }
        Entries.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
        { return CanonicalJson(Left) < CanonicalJson(Right); });
        const TSharedRef<FJsonObject> Tagged = MakeShared<FJsonObject>(); Tagged->SetStringField(TEXT("kind"), TEXT("map")); Tagged->SetArrayField(TEXT("entries"), Entries);
        OutValue = MakeShared<FJsonValueObject>(Tagged); return true;
    }
    OutError = {TEXT("unsupported_type"), TEXT("The row field type is outside the bounded game-data codec")};
    return false;
}

bool UnrealMCP::GameDataValueCodec::Decode(
    const FProperty* Property, void* Value, const TSharedPtr<FJsonValue>& Input,
    int32 Depth, FUnrealMCPError& OutError)
{
    if (FailDepth(Depth, OutError) || IsUnsafe(Property) || Value == nullptr || !Input.IsValid())
    {
        if (OutError.Code.IsEmpty()) OutError = {TEXT("unsupported_type"), TEXT("The row field type is not supported")};
        return false;
    }
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
    {
        bool Parsed = false; if (!Input->TryGetBool(Parsed)) { OutError = {TEXT("invalid_row"), TEXT("A Boolean row field requires a JSON Boolean")}; return false; }
        Bool->SetPropertyValue(Value, Parsed); return true;
    }
    if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property); Numeric != nullptr && PropertyEnum(Property) == nullptr)
    {
        double Number = 0.0;
        if (!Input->TryGetNumber(Number) || !FMath::IsFinite(Number)
            || (Numeric->IsInteger() && (!FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
                || FMath::Abs(Number) > 9007199254740991.0 || !Numeric->CanHoldValue(static_cast<int64>(Number))))
            || (Numeric->IsFloatingPoint() && !Numeric->CanHoldValue(Number)))
        { OutError = {TEXT("invalid_row"), TEXT("A numeric row field requires one compatible finite JSON number")}; return false; }
        if (Numeric->IsInteger()) Numeric->SetIntPropertyValue(Value, static_cast<int64>(Number)); else Numeric->SetFloatingPointPropertyValue(Value, Number);
        return true;
    }
    if (UEnum* Enum = PropertyEnum(Property))
    {
        FString Name; const int64 Number = Input->TryGetString(Name) ? EnumValue(Enum, Name) : INDEX_NONE;
        if (Number == INDEX_NONE) { OutError = {TEXT("invalid_row"), TEXT("An enum row field requires one exact live enumerator name")}; return false; }
        if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property)) EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(Value, Number);
        else CastFieldChecked<FByteProperty>(Property)->SetPropertyValue(Value, static_cast<uint8>(Number));
        return true;
    }
    FString String;
    if (const FNameProperty* Name = CastField<FNameProperty>(Property)) { if (!Input->TryGetString(String) || String.Len() > 128) goto InvalidString; Name->SetPropertyValue(Value, FName(*String)); return true; }
    if (const FStrProperty* Str = CastField<FStrProperty>(Property)) { if (!Input->TryGetString(String) || String.Len() > UnrealMCP::MaxStringLength) goto InvalidString; Str->SetPropertyValue(Value, String); return true; }
    if (const FTextProperty* Text = CastField<FTextProperty>(Property)) { if (!Input->TryGetString(String) || String.Len() > UnrealMCP::MaxStringLength) goto InvalidString; Text->SetPropertyValue(Value, FText::FromString(String)); return true; }
    if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
    {
        if (!ReadReference(Input, String, OutError)) return false;
        UClass* Class = String.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *String, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if ((!String.IsEmpty() && Class == nullptr) || (Class != nullptr && !Class->IsChildOf(ClassProperty->MetaClass)) || !SafeReference(Class))
        { OutError = {TEXT("invalid_row"), TEXT("The class reference is missing, unsafe, or incompatible")}; return false; }
        ClassProperty->SetObjectPropertyValue(Value, Class); return true;
    }
    if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(Property))
    {
        if (!ReadReference(Input, String, OutError)) return false;
        const FSoftObjectPath Path(String);
        UObject* Resolved = String.IsEmpty() ? nullptr : Path.TryLoad();
        const FSoftClassProperty* SoftClass = CastField<FSoftClassProperty>(Property);
        const UClass* ResolvedClass = Cast<UClass>(Resolved);
        if ((!String.IsEmpty() && (!Path.IsValid() || Resolved == nullptr))
            || (SoftClass != nullptr && !String.IsEmpty() && (ResolvedClass == nullptr || !ResolvedClass->IsChildOf(SoftClass->MetaClass)))
            || (SoftClass == nullptr && Resolved != nullptr && !Resolved->IsA(Soft->PropertyClass)) || !SafeReference(Resolved))
        { OutError = {TEXT("invalid_row"), TEXT("The soft reference is missing, unsafe, or incompatible")}; return false; }
        Soft->SetPropertyValue(Value, FSoftObjectPtr(Path)); return true;
    }
    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        if (!ReadReference(Input, String, OutError)) return false;
        UObject* Object = String.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *String, nullptr, LOAD_NoWarn | LOAD_Quiet);
        if ((!String.IsEmpty() && Object == nullptr) || (Object != nullptr && !Object->IsA(ObjectProperty->PropertyClass)) || !SafeReference(Object))
        { OutError = {TEXT("invalid_row"), TEXT("The object reference is missing, unsafe, or incompatible")}; return false; }
        ObjectProperty->SetObjectPropertyValue(Value, Object); return true;
    }
    if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        const TSharedPtr<FJsonObject>* Tagged = nullptr; const TSharedPtr<FJsonObject>* Fields = nullptr; FString Kind;
        if (!Input->TryGetObject(Tagged) || Tagged == nullptr || !(*Tagged).IsValid()
            || !CodecHasOnlyFields(**Tagged, {TEXT("kind"), TEXT("fields")})
            || !(*Tagged)->TryGetStringField(TEXT("kind"), Kind) || Kind != TEXT("struct")
            || !(*Tagged)->TryGetObjectField(TEXT("fields"), Fields) || Fields == nullptr
            || (*Fields)->Values.Num() > UnrealMCP::MaxGameDataFields)
        { OutError = {TEXT("invalid_row"), TEXT("A struct row field requires tagged bounded fields")}; return false; }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Fields)->Values)
        {
            FProperty* Child = FindAuthoredProperty(StructProperty->Struct, Pair.Key);
            if (Child == nullptr || !Decode(Child, Child->ContainerPtrToValuePtr<void>(Value), Pair.Value, Depth + 1, OutError))
            { if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_row"), TEXT("A nested struct field is missing")}; return false; }
        }
        return true;
    }
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        if (!Input->TryGetArray(Items) || Items == nullptr || Items->Num() > UnrealMCP::MaxGameDataCollectionItems)
        { OutError = {TEXT("data_limit_exceeded"), TEXT("A row array is invalid or exceeds the collection limit")}; return false; }
        FScriptArrayHelper Helper(Array, Value); Helper.EmptyValues(); Helper.AddValues(Items->Num());
        for (int32 Index = 0; Index < Items->Num(); ++Index) if (!Decode(Array->Inner, Helper.GetRawPtr(Index), (*Items)[Index], Depth + 1, OutError)) return false;
        return true;
    }
    if (const FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        const TSharedPtr<FJsonObject>* Tagged = nullptr; const TArray<TSharedPtr<FJsonValue>>* Items = nullptr; FString Kind;
        if (!Input->TryGetObject(Tagged) || Tagged == nullptr || !(*Tagged).IsValid() || !CodecHasOnlyFields(**Tagged, {TEXT("kind"), TEXT("items")})
            || !(*Tagged)->TryGetStringField(TEXT("kind"), Kind) || Kind != TEXT("set") || !(*Tagged)->TryGetArrayField(TEXT("items"), Items)
            || Items == nullptr || Items->Num() > UnrealMCP::MaxGameDataCollectionItems)
        { OutError = {TEXT("data_limit_exceeded"), TEXT("A row set is invalid or exceeds the collection limit")}; return false; }
        FScriptSetHelper Helper(Set, Value); Helper.EmptyElements();
        for (const TSharedPtr<FJsonValue>& Item : *Items)
        {
            const int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
            if (!Decode(Set->ElementProp, Helper.GetElementPtr(Index), Item, Depth + 1, OutError)) return false;
        }
        Helper.Rehash();
        if (Helper.Num() != Items->Num()) { OutError = {TEXT("invalid_row"), TEXT("A row set contains duplicate values")}; return false; }
        return true;
    }
    if (const FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        const TSharedPtr<FJsonObject>* Tagged = nullptr; const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr; FString Kind;
        if (!Input->TryGetObject(Tagged) || Tagged == nullptr || !(*Tagged).IsValid() || !CodecHasOnlyFields(**Tagged, {TEXT("kind"), TEXT("entries")})
            || !(*Tagged)->TryGetStringField(TEXT("kind"), Kind) || Kind != TEXT("map") || !(*Tagged)->TryGetArrayField(TEXT("entries"), Entries)
            || Entries == nullptr || Entries->Num() > UnrealMCP::MaxGameDataCollectionItems)
        { OutError = {TEXT("data_limit_exceeded"), TEXT("A row map is invalid or exceeds the collection limit")}; return false; }
        FScriptMapHelper Helper(Map, Value); Helper.EmptyValues();
        for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!EntryValue->TryGetObject(Entry) || Entry == nullptr || !(*Entry).IsValid() || !CodecHasOnlyFields(**Entry, {TEXT("key"), TEXT("value")}))
            { OutError = {TEXT("invalid_row"), TEXT("A row map entry requires exactly key and value")}; return false; }
            const int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
            if (!Decode(Map->KeyProp, Helper.GetKeyPtr(Index), (*Entry)->Values.FindRef(TEXT("key")), Depth + 1, OutError)
                || !Decode(Map->ValueProp, Helper.GetValuePtr(Index), (*Entry)->Values.FindRef(TEXT("value")), Depth + 1, OutError)) return false;
        }
        Helper.Rehash();
        if (Helper.Num() != Entries->Num()) { OutError = {TEXT("invalid_row"), TEXT("A row map contains duplicate keys")}; return false; }
        return true;
    }
    OutError = {TEXT("unsupported_type"), TEXT("The row field type is outside the bounded game-data codec")};
    return false;

InvalidString:
    OutError = {TEXT("invalid_row"), TEXT("A textual row field requires one bounded JSON string")};
    return false;
}

bool UnrealMCP::GameDataValueCodec::ApplyFields(
    const UScriptStruct* Struct, void* Data, const TSharedPtr<FJsonObject>& Fields, FUnrealMCPError& OutError)
{
    if (Struct == nullptr || Data == nullptr || !Fields.IsValid() || Fields->Values.Num() > UnrealMCP::MaxGameDataFields)
    { OutError = {TEXT("data_limit_exceeded"), TEXT("Row fields are missing or exceed the configured field limit")}; return false; }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Fields->Values)
    {
        FProperty* Property = FindAuthoredProperty(Struct, Pair.Key);
        if (Property == nullptr)
        {
            OutError = {TEXT("invalid_row"), TEXT("A row field does not exist in the live schema")};
            OutError.Details->SetStringField(TEXT("field"), Pair.Key.Left(128));
            return false;
        }
        if (!Decode(Property, Property->ContainerPtrToValuePtr<void>(Data), Pair.Value, 0, OutError)) return false;
    }
    return true;
}

TSharedRef<FJsonObject> UnrealMCP::GameDataValueCodec::EncodeFields(
    const UScriptStruct* Struct, const void* Data, int32 Depth, FUnrealMCPError& OutError, bool& bSucceeded)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    bSucceeded = false;
    if (FailDepth(Depth, OutError)) return Result;
    if (Struct == nullptr || Data == nullptr) { OutError = {TEXT("invalid_schema"), TEXT("The live row schema is unavailable")}; return Result; }
    int32 Count = 0;
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        if (++Count > UnrealMCP::MaxGameDataFields) { OutError = {TEXT("data_limit_exceeded"), TEXT("The live row schema exceeds the field limit")}; return Result; }
        TSharedPtr<FJsonValue> Value;
        if (!Encode(*It, It->ContainerPtrToValuePtr<void>(Data), Depth, Value, OutError)) return Result;
        Result->SetField(Struct->GetAuthoredNameForField(*It), Value);
    }
    bSucceeded = true;
    return Result;
}
