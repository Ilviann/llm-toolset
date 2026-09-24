#pragma once

#include "UnrealMCPWireTypes.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

// Reflection only: this reader must remain usable without GameplayAbilities loaded.
namespace UnrealMCP::GameplayAttributeInspection
{
static bool IsAttribute(const UObject* Type)
{
    return Type != nullptr && Type->GetPathName() == TEXT("/Script/GameplayAbilities.GameplayAttribute");
}

static FString PathValue(FString Value)
{
    Value.TrimStartAndEndInline();
    if (Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\""))) Value = Value.Mid(1, Value.Len() - 2);
    Value = FPackageName::ExportTextPathToObjectPath(Value);
    return Value == TEXT("None") ? FString() : Value;
}

static TSharedRef<FUnrealMCPRecord> EncodeText(const FString& Text)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("kind"), TEXT("gameplay_attribute"));
    TMap<FString, FString> Fields;
    FString Body = Text.TrimStartAndEnd();
    bool bValid = Body.IsEmpty() || (Body.StartsWith(TEXT("(")) && Body.EndsWith(TEXT(")")));
    if (Body.Len() > 4096) bValid = false;
    if (bValid && !Body.IsEmpty())
    {
        Body = Body.Mid(1, Body.Len() - 2);
        bool bQuoted = false;
        int32 Start = 0;
        for (int32 Index = 0; Index <= Body.Len(); ++Index)
        {
            if (Index < Body.Len() && Body[Index] == TCHAR('"')) bQuoted = !bQuoted;
            if (Index != Body.Len() && (Body[Index] != TCHAR(',') || bQuoted)) continue;
            FString Key, Value;
            const FString Part = Body.Mid(Start, Index - Start);
            Start = Index + 1;
            if (Part.IsEmpty()) continue;
            if (!Part.Split(TEXT("="), &Key, &Value)) { bValid = false; break; }
            Key.TrimStartAndEndInline();
            if ((Key != TEXT("Attribute") && Key != TEXT("AttributeName") && Key != TEXT("AttributeOwner"))
                || Fields.Contains(Key)) { bValid = false; break; }
            Fields.Add(Key, PathValue(Value));
        }
        bValid &= !bQuoted;
    }
    FString Path = bValid ? Fields.FindRef(TEXT("Attribute")) : FString();
    FString Name = bValid ? Fields.FindRef(TEXT("AttributeName")) : FString();
    FString Owner = bValid ? Fields.FindRef(TEXT("AttributeOwner")) : FString();
    FString PathOwner, PathName;
    if (!Path.IsEmpty())
    {
        if (!Path.Split(TEXT(":"), &PathOwner, &PathName, ESearchCase::CaseSensitive, ESearchDir::FromEnd)) bValid = false;
        if (Owner.IsEmpty()) Owner = PathOwner;
        if (Name.IsEmpty()) Name = PathName;
    }
    const UStruct* OwnerType = bValid && !Owner.IsEmpty() ? FindObject<UStruct>(nullptr, *Owner) : nullptr;
    const FProperty* Property = OwnerType != nullptr && !PathName.IsEmpty()
        ? FindFProperty<FProperty>(OwnerType, *PathName) : nullptr;
    const bool bResolved = Property != nullptr && Property->GetPathName() == Path;
    bool bCompatible = false;
    if (bResolved)
    {
        const FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
        bCompatible = Numeric != nullptr && Numeric->IsFloatingPoint();
        if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            for (const UStruct* Type = Struct->Struct; Type != nullptr; Type = Type->GetSuperStruct())
                bCompatible |= Type->GetPathName() == TEXT("/Script/GameplayAbilities.GameplayAttributeData");
        }
        bCompatible &= Name == Property->GetName() && Owner == Property->GetOwnerStruct()->GetPathName();
    }
    Result->SetBoolField(TEXT("valid_export"), bValid);
    Result->SetBoolField(TEXT("resolved"), bResolved);
    Result->SetBoolField(TEXT("compatible"), bCompatible);
    Result->SetStringField(TEXT("attribute_name"), Name.Left(128));
    Result->SetStringField(TEXT("property_path"), Path.Left(512));
    Result->SetStringField(TEXT("owner_path"), Owner.Left(512));
    return Result;
}

static TSharedRef<FUnrealMCPRecord> Encode(const FStructProperty* Property, const void* Address)
{
    FString Text;
    Property->ExportText_Direct(Text, Address, nullptr, nullptr, PPF_None);
    return EncodeText(Text);
}
}
