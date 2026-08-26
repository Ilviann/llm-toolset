#include "UnrealMCPGameplayAttributeCodec.h"

#include "Dom/JsonObject.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr const TCHAR* GameplayAttributePath = TEXT("/Script/GameplayAbilities.GameplayAttribute");
constexpr const TCHAR* GameplayAttributeDataPath = TEXT("/Script/GameplayAbilities.GameplayAttributeData");

bool SplitFields(const FString& Input, TArray<FString>& OutFields)
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
            OutFields.Add(Input.Mid(Start, Index - Start).TrimStartAndEnd());
            Start = Index + 1;
        }
    }
    if (bQuoted || Depth != 0) return false;
    if (Start < Input.Len()) OutFields.Add(Input.Mid(Start).TrimStartAndEnd());
    return true;
}

FString NormalizePath(FString Value)
{
    Value.TrimStartAndEndInline();
    if (Value.Equals(TEXT("None"), ESearchCase::IgnoreCase)) return FString();
    if (Value.Len() >= 2 && Value[0] == TCHAR('"') && Value[Value.Len() - 1] == TCHAR('"'))
    {
        Value = Value.Mid(1, Value.Len() - 2);
        Value.ReplaceInline(TEXT("\\\""), TEXT("\""));
        Value.ReplaceInline(TEXT("\\\\"), TEXT("\\"));
    }
    return FPackageName::ExportTextPathToObjectPath(Value);
}

bool IsCompatibleAttribute(const FProperty* Property)
{
    if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
    {
        return Numeric->IsFloatingPoint();
    }
    const FStructProperty* Struct = CastField<FStructProperty>(Property);
    if (Struct == nullptr || Struct->Struct == nullptr) return false;
    UScriptStruct* AttributeData = FindObject<UScriptStruct>(nullptr, GameplayAttributeDataPath);
    if (AttributeData == nullptr)
    {
        AttributeData = LoadObject<UScriptStruct>(nullptr, GameplayAttributeDataPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    }
    return AttributeData != nullptr && Struct->Struct->IsChildOf(AttributeData);
}
}

bool UnrealMCP::GameplayAttributeCodec::IsType(const UObject* TypeObject)
{
    return TypeObject != nullptr && TypeObject->GetPathName() == GameplayAttributePath;
}

bool UnrealMCP::GameplayAttributeCodec::Encode(const FString& Text, TSharedPtr<FJsonObject>& OutValue)
{
    if (Text.Len() > 4096) return false;
    FString Interior = Text.TrimStartAndEnd();
    if (Interior.Len() < 2 || Interior[0] != TCHAR('(') || Interior[Interior.Len() - 1] != TCHAR(')')) return false;
    Interior = Interior.Mid(1, Interior.Len() - 2);

    FString FieldPath;
    FString StoredOwnerPath;
    bool bHasAttribute = false;
    bool bHasOwner = false;
    TArray<FString> Fields;
    if (!SplitFields(Interior, Fields) || Fields.Num() > 2) return false;
    for (const FString& Field : Fields)
    {
        FString Name;
        FString Value;
        if (!Field.Split(TEXT("="), &Name, &Value, ESearchCase::CaseSensitive, ESearchDir::FromStart)) return false;
        Name.TrimStartAndEndInline();
        if (Name == TEXT("Attribute") && !bHasAttribute)
        {
            FieldPath = NormalizePath(Value);
            bHasAttribute = true;
        }
        else if (Name == TEXT("AttributeOwner") && !bHasOwner)
        {
            StoredOwnerPath = NormalizePath(Value);
            bHasOwner = true;
        }
        else return false;
    }
    if (!bHasAttribute) return false;

    FString FieldOwnerPath;
    FString AttributeName;
    if (!FieldPath.IsEmpty()
        && !FieldPath.Split(TEXT(":"), &FieldOwnerPath, &AttributeName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
    {
        return false;
    }
    FieldOwnerPath = NormalizePath(FieldOwnerPath);
    const FString OwnerPath = !FieldOwnerPath.IsEmpty() ? FieldOwnerPath : StoredOwnerPath;
    UStruct* Owner = OwnerPath.IsEmpty() ? nullptr : FindObject<UStruct>(nullptr, *OwnerPath);
    if (Owner == nullptr && !OwnerPath.IsEmpty())
    {
        Owner = LoadObject<UStruct>(nullptr, *OwnerPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    }
    FProperty* Attribute = Owner != nullptr && !AttributeName.IsEmpty()
        ? FindFProperty<FProperty>(Owner, FName(*AttributeName)) : nullptr;

    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), TEXT("gameplay_attribute"));
    Result->SetBoolField(TEXT("resolved"), Attribute != nullptr);
    Result->SetBoolField(TEXT("compatible"), IsCompatibleAttribute(Attribute));
    Result->SetStringField(TEXT("name"), Attribute != nullptr ? Attribute->GetName() : AttributeName);
    Result->SetStringField(TEXT("property_path"), Attribute != nullptr ? Attribute->GetPathName() : FieldPath);
    const UStruct* ResolvedOwner = Attribute != nullptr ? Attribute->GetOwnerStruct() : Owner;
    Result->SetStringField(TEXT("owner_path"), ResolvedOwner != nullptr ? ResolvedOwner->GetPathName() : OwnerPath);
    OutValue = Result;
    return true;
}
