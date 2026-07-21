#include "UnrealMCPK2TypeCodec.h"

#include "EdGraphSchema_K2.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr int32 MaxContainerItems = 64;

bool HasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed)
{
    TSet<FString> Names;
    for (const TCHAR* Name : Allowed) Names.Add(Name);
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
    {
        if (!Names.Contains(Pair.Key)) return false;
    }
    return true;
}

FString ContainerName(EPinContainerType Type)
{
    switch (Type)
    {
    case EPinContainerType::Array: return TEXT("array");
    case EPinContainerType::Set: return TEXT("set");
    case EPinContainerType::Map: return TEXT("map");
    default: return TEXT("none");
    }
}

bool ReadContainer(const FString& Value, EPinContainerType& Out)
{
    if (Value == TEXT("none")) Out = EPinContainerType::None;
    else if (Value == TEXT("array")) Out = EPinContainerType::Array;
    else if (Value == TEXT("set")) Out = EPinContainerType::Set;
    else if (Value == TEXT("map")) Out = EPinContainerType::Map;
    else return false;
    return true;
}

bool IsReferenceCategory(const FName& Category)
{
    return Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_Class
        || Category == UEdGraphSchema_K2::PC_SoftObject || Category == UEdGraphSchema_K2::PC_SoftClass;
}

bool IsStringCategory(const FName& Category)
{
    return Category == UEdGraphSchema_K2::PC_Name || Category == UEdGraphSchema_K2::PC_String
        || Category == UEdGraphSchema_K2::PC_Text || Category == UEdGraphSchema_K2::PC_Byte;
}

bool ResolveCategory(
    const FString& Category,
    const FString& Subcategory,
    const FString& TypeObjectPath,
    FName& OutCategory,
    FName& OutSubcategory,
    UObject*& OutTypeObject,
    FUnrealMCPError& OutError)
{
    static const TMap<FString, FName> Categories = {
        {TEXT("boolean"), UEdGraphSchema_K2::PC_Boolean},
        {TEXT("byte"), UEdGraphSchema_K2::PC_Byte},
        {TEXT("int"), UEdGraphSchema_K2::PC_Int},
        {TEXT("int64"), UEdGraphSchema_K2::PC_Int64},
        {TEXT("real"), UEdGraphSchema_K2::PC_Real},
        {TEXT("name"), UEdGraphSchema_K2::PC_Name},
        {TEXT("string"), UEdGraphSchema_K2::PC_String},
        {TEXT("text"), UEdGraphSchema_K2::PC_Text},
        {TEXT("enum"), UEdGraphSchema_K2::PC_Byte},
        {TEXT("struct"), UEdGraphSchema_K2::PC_Struct},
        {TEXT("object"), UEdGraphSchema_K2::PC_Object},
        {TEXT("class"), UEdGraphSchema_K2::PC_Class},
        {TEXT("softobject"), UEdGraphSchema_K2::PC_SoftObject},
        {TEXT("softclass"), UEdGraphSchema_K2::PC_SoftClass},
    };
    const FName* NativeCategory = Categories.Find(Category);
    if (NativeCategory == nullptr)
    {
        OutError = {TEXT("unsupported_type"), TEXT("The K2 variable category is not supported")};
        return false;
    }
    OutCategory = *NativeCategory;
    OutSubcategory = NAME_None;
    OutTypeObject = nullptr;
    if (Category == TEXT("real"))
    {
        if (Subcategory != TEXT("float") && Subcategory != TEXT("double"))
        {
            OutError = {TEXT("invalid_argument"), TEXT("real variables require subcategory float or double")};
            return false;
        }
        OutSubcategory = Subcategory == TEXT("float") ? UEdGraphSchema_K2::PC_Float : UEdGraphSchema_K2::PC_Double;
    }
    else if (!Subcategory.IsEmpty())
    {
        OutError = {TEXT("invalid_argument"), TEXT("subcategory is accepted only for real variables")};
        return false;
    }

    const bool bNeedsObject = Category == TEXT("enum") || Category == TEXT("struct") || IsReferenceCategory(OutCategory);
    if (bNeedsObject != !TypeObjectPath.IsEmpty())
    {
        OutError = {TEXT("invalid_argument"), TEXT("type_object is required exactly for enum, struct, and reference categories")};
        return false;
    }
    if (!bNeedsObject) return true;
    if (!TypeObjectPath.StartsWith(TEXT("/")) || TypeObjectPath.Contains(TEXT("..")) || TypeObjectPath.Contains(TEXT("\\"))
        || TypeObjectPath.Len() > 512)
    {
        OutError = {TEXT("invalid_argument"), TEXT("type_object must be one bounded Unreal object path")};
        return false;
    }
    OutTypeObject = LoadObject<UObject>(nullptr, *TypeObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (OutTypeObject == nullptr)
    {
        OutError = {TEXT("unsupported_type"), TEXT("The requested K2 type object was not found")};
        return false;
    }
    if (Category == TEXT("enum"))
    {
        UEnum* Enum = Cast<UEnum>(OutTypeObject);
        if (Enum == nullptr || !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Enum))
        {
            OutError = {TEXT("unsupported_type"), TEXT("The requested enum is not a live Blueprint variable type")};
            return false;
        }
    }
    else if (Category == TEXT("struct"))
    {
        UScriptStruct* Struct = Cast<UScriptStruct>(OutTypeObject);
        if (Struct == nullptr || !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Struct))
        {
            OutError = {TEXT("unsupported_type"), TEXT("The requested struct is not a live Blueprint variable type")};
            return false;
        }
    }
    else
    {
        UClass* Class = Cast<UClass>(OutTypeObject);
        if (Class == nullptr || Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists)
            || !UEdGraphSchema_K2::IsAllowableBlueprintVariableType(Class))
        {
            OutError = {TEXT("unsupported_type"), TEXT("The requested class is not a live Blueprint variable type")};
            return false;
        }
    }
    return true;
}

TSharedRef<FJsonObject> EncodeTerminal(FName Category, FName Subcategory, const UObject* TypeObject)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    FString WireCategory = Category.ToString().ToLower();
    if (Category == UEdGraphSchema_K2::PC_Boolean) WireCategory = TEXT("boolean");
    else if (Category == UEdGraphSchema_K2::PC_Byte && Cast<UEnum>(TypeObject) != nullptr) WireCategory = TEXT("enum");
    Result->SetStringField(TEXT("category"), WireCategory);
    if (!Subcategory.IsNone()) Result->SetStringField(TEXT("subcategory"), Subcategory.ToString().ToLower());
    if (TypeObject != nullptr) Result->SetStringField(TEXT("type_object"), TypeObject->GetPathName());
    return Result;
}

bool DecodeTerminal(const FJsonObject& Object, FEdGraphTerminalType& Out, FUnrealMCPError& OutError)
{
    if (!HasOnlyFields(Object, {TEXT("category"), TEXT("subcategory"), TEXT("type_object")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("A map value_type contains an unknown field")};
        return false;
    }
    FString Category;
    FString Subcategory;
    FString TypeObjectPath;
    if (!Object.TryGetStringField(TEXT("category"), Category)
        || (Object.HasField(TEXT("subcategory")) && !Object.TryGetStringField(TEXT("subcategory"), Subcategory))
        || (Object.HasField(TEXT("type_object")) && !Object.TryGetStringField(TEXT("type_object"), TypeObjectPath)))
    {
        OutError = {TEXT("invalid_argument"), TEXT("A K2 terminal type has invalid field types")};
        return false;
    }
    UObject* TypeObject = nullptr;
    if (!ResolveCategory(Category, Subcategory, TypeObjectPath, Out.TerminalCategory, Out.TerminalSubCategory, TypeObject, OutError)) return false;
    Out.TerminalSubCategoryObject = TypeObject;
    return true;
}

FString Quote(const FString& Input)
{
    FString Escaped = Input;
    Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
    return TEXT("\"") + Escaped + TEXT("\"");
}

FString Unquote(FString Input)
{
    Input.TrimStartAndEndInline();
    if (Input.Len() >= 2 && Input[0] == TCHAR('"') && Input[Input.Len() - 1] == TCHAR('"'))
    {
        Input = Input.Mid(1, Input.Len() - 2);
        Input.ReplaceInline(TEXT("\\\""), TEXT("\""));
        Input.ReplaceInline(TEXT("\\\\"), TEXT("\\"));
    }
    return Input;
}

bool SplitTopLevel(const FString& Input, TArray<FString>& Out)
{
    int32 Depth = 0;
    bool bQuoted = false;
    bool bEscaped = false;
    int32 Start = 0;
    for (int32 Index = 0; Index < Input.Len(); ++Index)
    {
        const TCHAR Character = Input[Index];
        if (bQuoted)
        {
            if (bEscaped) bEscaped = false;
            else if (Character == TCHAR('\\')) bEscaped = true;
            else if (Character == TCHAR('"')) bQuoted = false;
            continue;
        }
        if (Character == TCHAR('"')) bQuoted = true;
        else if (Character == TCHAR('(')) ++Depth;
        else if (Character == TCHAR(')'))
        {
            if (--Depth < 0) return false;
        }
        else if (Character == TCHAR(',') && Depth == 0)
        {
            Out.Add(Input.Mid(Start, Index - Start).TrimStartAndEnd());
            Start = Index + 1;
        }
    }
    if (bQuoted || Depth != 0) return false;
    if (Start < Input.Len()) Out.Add(Input.Mid(Start).TrimStartAndEnd());
    else if (!Input.IsEmpty()) Out.Add(FString());
    return true;
}

bool ReadAtom(
    FName Category,
    const UObject* TypeObject,
    const TSharedPtr<FJsonObject>& Value,
    bool bContainerElement,
    FString& Out,
    FUnrealMCPError& OutError)
{
    if (!Value.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("A variable default atom must be an object")};
        return false;
    }
    FString Kind;
    if (!Value->TryGetStringField(TEXT("kind"), Kind))
    {
        OutError = {TEXT("invalid_argument"), TEXT("A variable default atom requires kind")};
        return false;
    }
    if (IsReferenceCategory(Category))
    {
        FString Path;
        if (Kind != TEXT("reference") || !HasOnlyFields(*Value, {TEXT("kind"), TEXT("path")})
            || !Value->TryGetStringField(TEXT("path"), Path) || Path.Len() > 512 || Path.Contains(TEXT("..")) || Path.Contains(TEXT("\\")))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Reference defaults require one explicit bounded reference path")};
            return false;
        }
        if (Path.IsEmpty())
        {
            Out.Reset();
            return true;
        }
        UObject* Resolved = LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
        const UClass* RequiredClass = Cast<UClass>(TypeObject);
        const bool bClassReference = Category == UEdGraphSchema_K2::PC_Class || Category == UEdGraphSchema_K2::PC_SoftClass;
        UClass* ResolvedClass = Cast<UClass>(Resolved);
        if (Resolved == nullptr || RequiredClass == nullptr
            || (bClassReference ? (ResolvedClass == nullptr || !ResolvedClass->IsChildOf(RequiredClass)) : !Resolved->IsA(RequiredClass)))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The reference default does not resolve to the requested compatible type")};
            return false;
        }
        Out = bContainerElement ? Quote(Resolved->GetPathName()) : Resolved->GetPathName();
        return true;
    }
    if (Kind != TEXT("literal") || !HasOnlyFields(*Value, {TEXT("kind"), TEXT("value")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("This K2 type requires an explicit literal default")};
        return false;
    }
    const TSharedPtr<FJsonValue> Literal = Value->Values.FindRef(TEXT("value"));
    if (!Literal.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("literal default is missing value")};
        return false;
    }
    if (Category == UEdGraphSchema_K2::PC_Boolean)
    {
        bool Boolean = false;
        if (!Literal->TryGetBool(Boolean))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Boolean defaults require a JSON boolean")};
            return false;
        }
        Out = Boolean ? TEXT("true") : TEXT("false");
    }
    else if (Category == UEdGraphSchema_K2::PC_Int || Category == UEdGraphSchema_K2::PC_Int64 || Category == UEdGraphSchema_K2::PC_Real
        || (Category == UEdGraphSchema_K2::PC_Byte && TypeObject == nullptr))
    {
        double Number = 0.0;
        if (!Literal->TryGetNumber(Number) || !FMath::IsFinite(Number))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Numeric defaults require one finite JSON number")};
            return false;
        }
        if (Category != UEdGraphSchema_K2::PC_Real && !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Integer defaults must be integral")};
            return false;
        }
        if (Category == UEdGraphSchema_K2::PC_Byte && (Number < 0.0 || Number > 255.0))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Byte defaults must be between 0 and 255")};
            return false;
        }
        Out = Category == UEdGraphSchema_K2::PC_Real
            ? FString::SanitizeFloat(Number, 0) : FString::Printf(TEXT("%.0f"), Number);
    }
    else if (IsStringCategory(Category) || (Category == UEdGraphSchema_K2::PC_Byte && Cast<UEnum>(TypeObject) != nullptr))
    {
        FString String;
        if (!Literal->TryGetString(String) || String.Len() > 4096)
        {
            OutError = {TEXT("invalid_argument"), TEXT("Textual and enum defaults require one bounded JSON string")};
            return false;
        }
        if (const UEnum* Enum = Cast<UEnum>(TypeObject); Enum != nullptr && Enum->GetIndexByNameString(String) == INDEX_NONE)
        {
            OutError = {TEXT("invalid_argument"), TEXT("The enum default is not one exact live enumerator name")};
            return false;
        }
        Out = bContainerElement ? Quote(String) : String;
    }
    else
    {
        OutError = {TEXT("unsupported_type"), TEXT("Non-default struct literals are not supported by this bounded codec")};
        return false;
    }
    return true;
}

TSharedRef<FJsonObject> EncodeAtom(FName Category, const UObject* TypeObject, const FString& Text)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    if (IsReferenceCategory(Category))
    {
        Result->SetStringField(TEXT("kind"), TEXT("reference"));
        FString Path = Unquote(Text);
        if (Path.Equals(TEXT("None"), ESearchCase::IgnoreCase)) Path.Reset();
        else Path = FPackageName::ExportTextPathToObjectPath(Path);
        Result->SetStringField(TEXT("path"), Path);
        return Result;
    }
    Result->SetStringField(TEXT("kind"), TEXT("literal"));
    if (Category == UEdGraphSchema_K2::PC_Boolean)
    {
        Result->SetBoolField(TEXT("value"), Text.Equals(TEXT("true"), ESearchCase::IgnoreCase));
    }
    else if (Category == UEdGraphSchema_K2::PC_Int || Category == UEdGraphSchema_K2::PC_Int64 || Category == UEdGraphSchema_K2::PC_Real
        || (Category == UEdGraphSchema_K2::PC_Byte && TypeObject == nullptr))
    {
        double Number = 0.0;
        if (LexTryParseString(Number, *Text)) Result->SetNumberField(TEXT("value"), Number);
        else
        {
            Result->SetStringField(TEXT("kind"), TEXT("unavailable"));
            Result->RemoveField(TEXT("value"));
        }
    }
    else if (IsStringCategory(Category) || Cast<UEnum>(TypeObject) != nullptr)
    {
        Result->SetStringField(TEXT("value"), Unquote(Text));
    }
    else
    {
        Result->SetStringField(TEXT("kind"), TEXT("unavailable"));
    }
    return Result;
}
}

namespace UnrealMCP::K2TypeCodec
{
TSharedRef<FJsonObject> EncodeType(const FEdGraphPinType& Type)
{
    const TSharedRef<FJsonObject> Result = EncodeTerminal(Type.PinCategory, Type.PinSubCategory, Type.PinSubCategoryObject.Get());
    Result->SetStringField(TEXT("container"), ContainerName(Type.ContainerType));
    if (Type.IsMap())
    {
        Result->SetObjectField(TEXT("value_type"), EncodeTerminal(
            Type.PinValueType.TerminalCategory, Type.PinValueType.TerminalSubCategory, Type.PinValueType.TerminalSubCategoryObject.Get()));
    }
    const FString Category = Result->GetStringField(TEXT("category"));
    Result->SetBoolField(TEXT("supported"), Category == TEXT("boolean") || Category == TEXT("byte") || Category == TEXT("int")
        || Category == TEXT("int64") || Category == TEXT("real") || Category == TEXT("name") || Category == TEXT("string")
        || Category == TEXT("text") || Category == TEXT("enum") || Category == TEXT("struct") || Category == TEXT("object")
        || Category == TEXT("class") || Category == TEXT("softobject") || Category == TEXT("softclass"));
    return Result;
}

bool DecodeType(const TSharedPtr<FJsonObject>& Value, FEdGraphPinType& OutType, FUnrealMCPError& OutError)
{
    if (!Value.IsValid() || !HasOnlyFields(*Value, {TEXT("category"), TEXT("subcategory"), TEXT("type_object"), TEXT("container"), TEXT("value_type")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("type must be one exact K2 type object")};
        return false;
    }
    FString Category;
    FString Subcategory;
    FString TypeObjectPath;
    FString Container;
    if (!Value->TryGetStringField(TEXT("category"), Category) || !Value->TryGetStringField(TEXT("container"), Container)
        || (Value->HasField(TEXT("subcategory")) && !Value->TryGetStringField(TEXT("subcategory"), Subcategory))
        || (Value->HasField(TEXT("type_object")) && !Value->TryGetStringField(TEXT("type_object"), TypeObjectPath))
        || !ReadContainer(Container, OutType.ContainerType))
    {
        OutError = {TEXT("invalid_argument"), TEXT("type contains invalid K2 type fields")};
        return false;
    }
    UObject* TypeObject = nullptr;
    if (!ResolveCategory(Category, Subcategory, TypeObjectPath, OutType.PinCategory, OutType.PinSubCategory, TypeObject, OutError)) return false;
    OutType.PinSubCategoryObject = TypeObject;
    const TSharedPtr<FJsonObject>* ValueType = nullptr;
    if (OutType.IsMap())
    {
        if (!Value->TryGetObjectField(TEXT("value_type"), ValueType) || ValueType == nullptr
            || !DecodeTerminal(**ValueType, OutType.PinValueType, OutError)) return false;
    }
    else if (Value->HasField(TEXT("value_type")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("value_type is accepted only for map variables")};
        return false;
    }
    OutType.bIsReference = false;
    OutType.bIsConst = false;
    OutType.bIsWeakPointer = false;
    return true;
}

TSharedRef<FJsonObject> EncodeDefault(const FEdGraphPinType& Type, const FString& DefaultText)
{
    if (DefaultText.IsEmpty())
    {
        const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("kind"), TEXT("engine_default"));
        return Result;
    }
    if (!Type.IsContainer()) return EncodeAtom(Type.PinCategory, Type.PinSubCategoryObject.Get(), DefaultText);
    FString Interior = DefaultText.TrimStartAndEnd();
    if (Interior.Len() < 2 || Interior[0] != TCHAR('(') || Interior[Interior.Len() - 1] != TCHAR(')'))
    {
        const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("kind"), TEXT("unavailable"));
        return Result;
    }
    Interior = Interior.Mid(1, Interior.Len() - 2);
    TArray<FString> Parts;
    if (!SplitTopLevel(Interior, Parts) || Parts.Num() > MaxContainerItems)
    {
        const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("kind"), TEXT("unavailable"));
        return Result;
    }
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    if (!Type.IsMap())
    {
        Result->SetStringField(TEXT("kind"), Type.IsArray() ? TEXT("array") : TEXT("set"));
        TArray<TSharedPtr<FJsonValue>> Items;
        for (const FString& Part : Parts) Items.Add(MakeShared<FJsonValueObject>(EncodeAtom(Type.PinCategory, Type.PinSubCategoryObject.Get(), Part)));
        Result->SetArrayField(TEXT("items"), Items);
        return Result;
    }
    Result->SetStringField(TEXT("kind"), TEXT("map"));
    TArray<TSharedPtr<FJsonValue>> Entries;
    for (FString Part : Parts)
    {
        Part.TrimStartAndEndInline();
        if (Part.Len() < 2 || Part[0] != TCHAR('(') || Part[Part.Len() - 1] != TCHAR(')'))
        {
            Result->SetStringField(TEXT("kind"), TEXT("unavailable"));
            return Result;
        }
        TArray<FString> Pair;
        if (!SplitTopLevel(Part.Mid(1, Part.Len() - 2), Pair) || Pair.Num() != 2)
        {
            Result->SetStringField(TEXT("kind"), TEXT("unavailable"));
            return Result;
        }
        const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
        Entry->SetObjectField(TEXT("key"), EncodeAtom(Type.PinCategory, Type.PinSubCategoryObject.Get(), Pair[0]));
        Entry->SetObjectField(TEXT("value"), EncodeAtom(Type.PinValueType.TerminalCategory, Type.PinValueType.TerminalSubCategoryObject.Get(), Pair[1]));
        Entries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Result->SetArrayField(TEXT("entries"), Entries);
    return Result;
}

bool DecodeDefault(const FEdGraphPinType& Type, const TSharedPtr<FJsonObject>& Value, FString& OutDefaultText, FUnrealMCPError& OutError)
{
    if (!Value.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("default must be one exact tagged value object")};
        return false;
    }
    FString Kind;
    if (!Value->TryGetStringField(TEXT("kind"), Kind))
    {
        OutError = {TEXT("invalid_argument"), TEXT("default requires one tagged kind")};
        return false;
    }
    if (Kind == TEXT("engine_default"))
    {
        if (!HasOnlyFields(*Value, {TEXT("kind")}))
        {
            OutError = {TEXT("invalid_argument"), TEXT("engine_default accepts no value fields")};
            return false;
        }
        OutDefaultText.Reset();
        return true;
    }
    if (!Type.IsContainer())
    {
        if (!ReadAtom(Type.PinCategory, Type.PinSubCategoryObject.Get(), Value, false, OutDefaultText, OutError)) return false;
    }
    else if (Type.IsArray() || Type.IsSet())
    {
        const TCHAR* Expected = Type.IsArray() ? TEXT("array") : TEXT("set");
        const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
        if (Kind != Expected || !HasOnlyFields(*Value, {TEXT("kind"), TEXT("items")})
            || !Value->TryGetArrayField(TEXT("items"), Items) || Items == nullptr || Items->Num() > MaxContainerItems)
        {
            OutError = {TEXT("invalid_argument"), TEXT("The container default kind must exactly match its K2 type")};
            return false;
        }
        TArray<FString> Encoded;
        for (const TSharedPtr<FJsonValue>& Item : *Items)
        {
            const TSharedPtr<FJsonObject>* Object = nullptr;
            FString Atom;
            if (!Item.IsValid() || !Item->TryGetObject(Object) || Object == nullptr
                || !ReadAtom(Type.PinCategory, Type.PinSubCategoryObject.Get(), *Object, true, Atom, OutError)) return false;
            Encoded.Add(Atom);
        }
        OutDefaultText = TEXT("(") + FString::Join(Encoded, TEXT(",")) + TEXT(")");
    }
    else
    {
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
        if (Kind != TEXT("map") || !HasOnlyFields(*Value, {TEXT("kind"), TEXT("entries")})
            || !Value->TryGetArrayField(TEXT("entries"), Entries) || Entries == nullptr || Entries->Num() > MaxContainerItems)
        {
            OutError = {TEXT("invalid_argument"), TEXT("Map defaults require one bounded entries array")};
            return false;
        }
        TArray<FString> Encoded;
        for (const TSharedPtr<FJsonValue>& Item : *Entries)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            const TSharedPtr<FJsonObject>* Key = nullptr;
            const TSharedPtr<FJsonObject>* MapValue = nullptr;
            FString KeyText;
            FString ValueText;
            if (!Item.IsValid() || !Item->TryGetObject(Entry) || Entry == nullptr
                || !HasOnlyFields(**Entry, {TEXT("key"), TEXT("value")})
                || !(*Entry)->TryGetObjectField(TEXT("key"), Key) || Key == nullptr
                || !(*Entry)->TryGetObjectField(TEXT("value"), MapValue) || MapValue == nullptr
                || !ReadAtom(Type.PinCategory, Type.PinSubCategoryObject.Get(), *Key, true, KeyText, OutError)
                || !ReadAtom(Type.PinValueType.TerminalCategory, Type.PinValueType.TerminalSubCategoryObject.Get(), *MapValue, true, ValueText, OutError)) return false;
            Encoded.Add(TEXT("(") + KeyText + TEXT(",") + ValueText + TEXT(")"));
        }
        OutDefaultText = TEXT("(") + FString::Join(Encoded, TEXT(",")) + TEXT(")");
    }
    FString ValidationError;
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    if (!OutDefaultText.IsEmpty() && !IsReferenceCategory(Type.PinCategory)
        && !Schema->DefaultValueSimpleValidation(Type, NAME_None, OutDefaultText, nullptr, FText::GetEmpty(), &ValidationError))
    {
        OutError = {TEXT("invalid_argument"), TEXT("The explicit default is incompatible with the live K2 type")};
        return false;
    }
    return true;
}
}
