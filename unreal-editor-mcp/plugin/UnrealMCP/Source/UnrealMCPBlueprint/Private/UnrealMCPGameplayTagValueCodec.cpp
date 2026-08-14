#include "UnrealMCPGameplayTagValueCodec.h"

#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "UnrealMCPVersion.h"
#include "UnrealMCPWireTypes.h"
#include "UObject/UnrealType.h"

namespace
{
using EPropertyKind = UnrealMCP::GameplayTagValueCodec::EPropertyKind;

bool FailLimit(const TCHAR* Message, FUnrealMCPError& OutError)
{
    OutError = {TEXT("data_limit_exceeded"), Message};
    return false;
}

bool FailInvalid(const TCHAR* InvalidCode, FUnrealMCPError& OutError)
{
    OutError = {
        InvalidCode,
        TEXT("Gameplay Tag values require exact canonical names registered in the live project")};
    return false;
}

bool ReadExactTag(
    const TSharedPtr<FUnrealMCPValue>& Input,
    bool bAllowEmpty,
    const TCHAR* InvalidCode,
    FGameplayTag& OutTag,
    FUnrealMCPError& OutError)
{
    FString Name;
    if (!Input.IsValid() || !Input->TryGetString(Name))
    {
        return FailInvalid(InvalidCode, OutError);
    }
    if (Name.Len() > UnrealMCP::MaxGameplayTagChars)
    {
        return FailLimit(TEXT("A Gameplay Tag name exceeds the configured character limit"), OutError);
    }
    if (Name.IsEmpty())
    {
        if (!bAllowEmpty)
        {
            return FailInvalid(InvalidCode, OutError);
        }
        OutTag = FGameplayTag();
        return true;
    }
    if (!FGameplayTag::IsValidGameplayTagString(Name))
    {
        return FailInvalid(InvalidCode, OutError);
    }
    const FGameplayTag Resolved = UGameplayTagsManager::Get().RequestGameplayTag(FName(*Name), false);
    if (!Resolved.IsValid() || !Resolved.GetTagName().ToString().Equals(Name, ESearchCase::CaseSensitive))
    {
        // RequestGameplayTag follows redirects and FName comparisons are case-insensitive. Exact
        // read-back therefore rejects redirected and non-canonical spellings without normalizing them.
        return FailInvalid(InvalidCode, OutError);
    }
    OutTag = Resolved;
    return true;
}

bool EncodeTagName(const FGameplayTag& Tag, FString& OutName, FUnrealMCPError& OutError)
{
    OutName = Tag.GetTagName().IsNone() ? FString() : Tag.GetTagName().ToString();
    if (OutName.Len() > UnrealMCP::MaxGameplayTagChars)
    {
        return FailLimit(TEXT("A stored Gameplay Tag name exceeds the configured character limit"), OutError);
    }
    return true;
}
}

UnrealMCP::GameplayTagValueCodec::EPropertyKind
UnrealMCP::GameplayTagValueCodec::Classify(const FProperty* Property)
{
    const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
    if (StructProperty == nullptr || StructProperty->Struct == nullptr)
    {
        return EPropertyKind::None;
    }
    if (StructProperty->Struct == FGameplayTag::StaticStruct())
    {
        return EPropertyKind::Tag;
    }
    if (StructProperty->Struct == FGameplayTagContainer::StaticStruct())
    {
        return EPropertyKind::Container;
    }
    return EPropertyKind::None;
}

const TCHAR* UnrealMCP::GameplayTagValueCodec::TypeName(EPropertyKind Kind)
{
    switch (Kind)
    {
    case EPropertyKind::Tag: return TEXT("gameplay_tag");
    case EPropertyKind::Container: return TEXT("gameplay_tag_container");
    default: return TEXT("unsupported");
    }
}

bool UnrealMCP::GameplayTagValueCodec::Encode(
    const FProperty* Property,
    const void* Value,
    TSharedPtr<FUnrealMCPValue>& OutValue,
    FUnrealMCPError& OutError)
{
    const EPropertyKind Kind = Classify(Property);
    if (Kind == EPropertyKind::None || Value == nullptr)
    {
        OutError = {TEXT("unsupported_type"), TEXT("The property is not an exact Gameplay Tag value")};
        return false;
    }
    if (Kind == EPropertyKind::Tag)
    {
        FString Name;
        if (!EncodeTagName(*static_cast<const FGameplayTag*>(Value), Name, OutError))
        {
            return false;
        }
        OutValue = MakeShared<FUnrealMCPValueString>(Name);
        return true;
    }

    const TArray<FGameplayTag>& ExplicitTags =
        static_cast<const FGameplayTagContainer*>(Value)->GetGameplayTagArray();
    if (ExplicitTags.Num() > UnrealMCP::MaxGameplayTagsPerContainer)
    {
        return FailLimit(TEXT("A Gameplay Tag container exceeds the configured item limit"), OutError);
    }
    TArray<FString> Names;
    Names.Reserve(ExplicitTags.Num());
    for (const FGameplayTag& Tag : ExplicitTags)
    {
        FString Name;
        if (!EncodeTagName(Tag, Name, OutError))
        {
            return false;
        }
        Names.Add(MoveTemp(Name));
    }
    Names.Sort([](const FString& Left, const FString& Right)
    {
        return Left.Compare(Right, ESearchCase::CaseSensitive) < 0;
    });
    TArray<TSharedPtr<FUnrealMCPValue>> Items;
    Items.Reserve(Names.Num());
    for (FString& Name : Names)
    {
        Items.Add(MakeShared<FUnrealMCPValueString>(MoveTemp(Name)));
    }
    OutValue = MakeShared<FUnrealMCPValueArray>(Items);
    return true;
}

bool UnrealMCP::GameplayTagValueCodec::Decode(
    const FProperty* Property,
    void* Value,
    const TSharedPtr<FUnrealMCPValue>& Input,
    const TCHAR* InvalidCode,
    FUnrealMCPError& OutError)
{
    const EPropertyKind Kind = Classify(Property);
    if (Kind == EPropertyKind::None || Value == nullptr || InvalidCode == nullptr)
    {
        OutError = {TEXT("unsupported_type"), TEXT("The property is not an exact Gameplay Tag value")};
        return false;
    }
    if (Kind == EPropertyKind::Tag)
    {
        FGameplayTag Tag;
        if (!ReadExactTag(Input, true, InvalidCode, Tag, OutError))
        {
            return false;
        }
        *static_cast<FGameplayTag*>(Value) = Tag;
        return true;
    }

    const TArray<TSharedPtr<FUnrealMCPValue>>* Items = nullptr;
    if (!Input.IsValid() || !Input->TryGetArray(Items) || Items == nullptr)
    {
        return FailInvalid(InvalidCode, OutError);
    }
    if (Items->Num() > UnrealMCP::MaxGameplayTagsPerContainer)
    {
        return FailLimit(TEXT("A Gameplay Tag container exceeds the configured item limit"), OutError);
    }
    TArray<FGameplayTag> Tags;
    Tags.Reserve(Items->Num());
    TSet<FName> Seen;
    for (const TSharedPtr<FUnrealMCPValue>& Item : *Items)
    {
        FGameplayTag Tag;
        if (!ReadExactTag(Item, false, InvalidCode, Tag, OutError))
        {
            return false;
        }
        if (Seen.Contains(Tag.GetTagName()))
        {
            return FailInvalid(InvalidCode, OutError);
        }
        Seen.Add(Tag.GetTagName());
        Tags.Add(Tag);
    }
    *static_cast<FGameplayTagContainer*>(Value) = FGameplayTagContainer::CreateFromArray(Tags);
    return true;
}
