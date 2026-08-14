#include "UnrealMCPStructuredDataInspection.h"

#include "Misc/SecureHash.h"
#include "UnrealMCPGameDataValueCodec.h"
#include "UnrealMCPGameplayTagValueCodec.h"
#include "UnrealMCPJsonCodec.h"
#include "UnrealMCPVersion.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace UnrealMCP::StructuredDataInspection::Private
{
constexpr int32 MaxProperties = 256;

bool IsUnreserved(uint8 Byte)
{
    return (Byte >= 'A' && Byte <= 'Z') || (Byte >= 'a' && Byte <= 'z')
        || (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '.' || Byte == '_' || Byte == '~';
}

FString Sha1(const FString& Material)
{
    const FTCHARToUTF8 Encoded(*Material);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

bool IsCandidate(const FProperty* Property, bool bRequireAuthoredProperty)
{
    if (Property == nullptr || Property->ArrayDim != 1
        || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditorOnly))
    {
        return false;
    }
    return !bRequireAuthoredProperty
        || Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
}

FString AuthoredName(const UStruct* Type, const FProperty* Property)
{
    return Type != nullptr ? Type->GetAuthoredNameForField(Property) : Property->GetName();
}

FString DeclaredBy(const FProperty* Property)
{
    const UStruct* Owner = Property != nullptr ? Property->GetOwnerStruct() : nullptr;
    return Owner != nullptr ? Owner->GetPathName() : FString();
}

TArray<FProperty*> Properties(const FUnrealMCPStructuredDataSource& Source, FUnrealMCPError& OutError)
{
    TArray<FProperty*> Result;
    if (Source.Type == nullptr || Source.Data == nullptr)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Structured property inspection requires live data and a type")};
        return Result;
    }
    for (TFieldIterator<FProperty> It(Source.Type, EFieldIterationFlags::IncludeSuper); It; ++It)
    {
        if (IsCandidate(*It, Source.bRequireAuthoredProperty)) Result.Add(*It);
    }
    Result.Sort([&Source](const FProperty& Left, const FProperty& Right)
    {
        const FString LeftKey = DeclaredBy(&Left) + TEXT("|") + AuthoredName(Source.Type, &Left);
        const FString RightKey = DeclaredBy(&Right) + TEXT("|") + AuthoredName(Source.Type, &Right);
        return LeftKey < RightKey;
    });
    if (Result.Num() > MaxProperties)
    {
        OutError = {TEXT("data_limit_exceeded"), TEXT("The structured asset exceeds the reflected property limit")};
        Result.Reset();
    }
    return Result;
}

TSharedRef<FUnrealMCPRecord> Limitation(const FProperty* Property, const FUnrealMCPError& Error)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("kind"), TEXT("limitation"));
    Result->SetStringField(TEXT("code"), Error.Code.IsEmpty() ? TEXT("unsupported_type") : Error.Code);
    Result->SetStringField(TEXT("declared_type"), Property != nullptr ? Property->GetClass()->GetName() : FString());
    return Result;
}

TSharedRef<FUnrealMCPRecord> CollectionDescriptor(
    const FProperty* Property, int32 Count, const FString& Selector)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        Result->SetStringField(TEXT("kind"), TEXT("array"));
        Result->SetObjectField(TEXT("item_type"), GameDataValueCodec::EncodeType(Array->Inner));
    }
    else if (const FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        Result->SetStringField(TEXT("kind"), TEXT("set"));
        Result->SetObjectField(TEXT("item_type"), GameDataValueCodec::EncodeType(Set->ElementProp));
    }
    else if (const FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        Result->SetStringField(TEXT("kind"), TEXT("map"));
        Result->SetObjectField(TEXT("key_type"), GameDataValueCodec::EncodeType(Map->KeyProp));
        Result->SetObjectField(TEXT("value_type"), GameDataValueCodec::EncodeType(Map->ValueProp));
    }
    Result->SetNumberField(TEXT("count"), Count);
    Result->SetStringField(TEXT("selector"), Selector);
    return Result;
}

bool CollectionCount(const FProperty* Property, const void* Address, int32& OutCount)
{
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        OutCount = FScriptArrayHelper(Array, Address).Num(); return true;
    }
    if (const FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        OutCount = FScriptSetHelper(Set, Address).Num(); return true;
    }
    if (const FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        OutCount = FScriptMapHelper(Map, Address).Num(); return true;
    }
    return false;
}

bool SemanticValue(
    const FProperty* Property,
    const void* Address,
    UObject* Owner,
    const FString& Selector,
    int32 Depth,
    TSharedPtr<FUnrealMCPValue>& OutValue,
    FUnrealMCPError& OutError)
{
    if (Depth > UnrealMCP::MaxGameDataDepth)
    {
        OutError = {TEXT("data_limit_exceeded"), TEXT("Structured asset values exceed the supported depth")};
        return false;
    }
    if (GameplayTagValueCodec::Classify(Property) != GameplayTagValueCodec::EPropertyKind::None)
    {
        return GameDataValueCodec::Encode(Property, Address, Depth, OutValue, OutError);
    }
    int32 Count = 0;
    if (CollectionCount(Property, Address, Count))
    {
        OutValue = MakeShared<FUnrealMCPValueObject>(CollectionDescriptor(Property, Count, Selector));
        return true;
    }
    if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
    {
        const TSharedRef<FUnrealMCPRecord> Fields = MakeShared<FUnrealMCPRecord>();
        int32 FieldCount = 0;
        for (TFieldIterator<FProperty> It(Struct->Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
        {
            FProperty* Child = *It;
            if (!IsCandidate(Child, false)) continue;
            if (++FieldCount > UnrealMCP::MaxGameDataFields)
            {
                OutError = {TEXT("data_limit_exceeded"), TEXT("A nested struct exceeds the field limit")};
                return false;
            }
            const FString Name = AuthoredName(Struct->Struct, Child);
            TSharedPtr<FUnrealMCPValue> ChildValue;
            FUnrealMCPError ChildError;
            if (!SemanticValue(Child, Child->ContainerPtrToValuePtr<void>(Address), Owner,
                Selector + TEXT("/") + EncodeSelectorSegment(Name), Depth + 1, ChildValue, ChildError))
            {
                ChildValue = MakeShared<FUnrealMCPValueObject>(Limitation(Child, ChildError));
            }
            Fields->SetField(Name, ChildValue);
        }
        const TSharedRef<FUnrealMCPRecord> Tagged = MakeShared<FUnrealMCPRecord>();
        Tagged->SetStringField(TEXT("kind"), TEXT("struct"));
        Tagged->SetStringField(TEXT("type"), Struct->Struct != nullptr ? Struct->Struct->GetPathName() : FString());
        Tagged->SetObjectField(TEXT("fields"), Fields);
        OutValue = MakeShared<FUnrealMCPValueObject>(Tagged);
        return true;
    }
    return GameDataValueCodec::Encode(Property, Address, Depth, OutValue, OutError);
}

TSharedRef<FUnrealMCPRecord> PropertyRecord(
    const FUnrealMCPStructuredDataSource& Source,
    FProperty* Property,
    const FString& SelectorPrefix)
{
    const FString Name = AuthoredName(Source.Type, Property);
    const FString Selector = SelectorPrefix + TEXT("/") + EncodeSelectorSegment(Name);
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("name"), Name);
    Result->SetStringField(TEXT("declared_by"), DeclaredBy(Property));
    Result->SetObjectField(TEXT("type"), GameDataValueCodec::EncodeType(Property));
    TSharedPtr<FUnrealMCPValue> Value;
    FUnrealMCPError Error;
    if (!SemanticValue(Property, Property->ContainerPtrToValuePtr<void>(Source.Data), Source.Owner,
        Selector, 0, Value, Error))
    {
        Value = MakeShared<FUnrealMCPValueObject>(Limitation(Property, Error));
    }
    Result->SetField(TEXT("value"), Value);
    return Result;
}

TSharedRef<FUnrealMCPRecord> PageRecord(
    int32 PageIndex, int32 PageSize, int32 Total, int32 Returned, const FString& Snapshot)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    const int32 PageCount = Total == 0 ? 0 : (Total + PageSize - 1) / PageSize;
    Result->SetNumberField(TEXT("size"), PageSize);
    Result->SetNumberField(TEXT("index"), PageIndex);
    Result->SetNumberField(TEXT("count"), PageCount);
    Result->SetNumberField(TEXT("returned"), Returned);
    Result->SetNumberField(TEXT("total_items"), Total);
    Result->SetBoolField(TEXT("has_previous"), PageIndex > 0 && Total > 0);
    Result->SetBoolField(TEXT("has_next"), static_cast<int64>(PageIndex + 1) * PageSize < Total);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    return Result;
}

FString Canonical(const FProperty* Property, const void* Address)
{
    TSharedPtr<FUnrealMCPValue> Value;
    FUnrealMCPError Error;
    if (GameDataValueCodec::Encode(Property, Address, 0, Value, Error) && Value.IsValid())
    {
        FString Result;
        JsonCodec::SerializeValue(Value, Result);
        return Result;
    }
    FString Exported;
    Property->ExportText_Direct(Exported, Address, nullptr, nullptr, PPF_None);
    return Exported;
}

TArray<int32> SortedSetIndexes(const FSetProperty* Property, const void* Address)
{
    FScriptSetHelper Helper(Property, Address);
    TArray<int32> Result;
    for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index) if (Helper.IsValidIndex(Index)) Result.Add(Index);
    Result.Sort([&](int32 Left, int32 Right)
        { return Canonical(Property->ElementProp, Helper.GetElementPtr(Left))
            < Canonical(Property->ElementProp, Helper.GetElementPtr(Right)); });
    return Result;
}

TArray<int32> SortedMapIndexes(const FMapProperty* Property, const void* Address)
{
    FScriptMapHelper Helper(Property, Address);
    TArray<int32> Result;
    for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index) if (Helper.IsValidIndex(Index)) Result.Add(Index);
    Result.Sort([&](int32 Left, int32 Right)
        { return Canonical(Property->KeyProp, Helper.GetKeyPtr(Left))
            < Canonical(Property->KeyProp, Helper.GetKeyPtr(Right)); });
    return Result;
}

bool ParseIndex(const FString& Value, int32 Count, int32& OutIndex)
{
    if (Value.IsEmpty() || !LexTryParseString(OutIndex, *Value) || OutIndex < 0 || OutIndex >= Count)
    {
        return false;
    }
    return LexToString(OutIndex) == Value;
}

struct FResolvedValue
{
    FProperty* Property = nullptr;
    const void* Address = nullptr;
    FString Selector;
};

bool Resolve(
    const FUnrealMCPStructuredDataSource& Source,
    const FString& SelectorPrefix,
    const TArray<FString>& Segments,
    FResolvedValue& Out,
    FUnrealMCPError& OutError)
{
    if (Segments.IsEmpty())
    {
        OutError = {TEXT("invalid_argument"), TEXT("A field selector requires one field name")};
        return false;
    }
    const TArray<FProperty*> Candidates = Properties(Source, OutError);
    if (!OutError.Code.IsEmpty()) return false;
    FProperty* Property = nullptr;
    for (FProperty* Candidate : Candidates)
    {
        if (AuthoredName(Source.Type, Candidate) == Segments[0]) { Property = Candidate; break; }
    }
    if (Property == nullptr)
    {
        OutError = {TEXT("not_found"), TEXT("The selected structured field was not found")};
        return false;
    }
    const void* Address = Property->ContainerPtrToValuePtr<void>(Source.Data);
    FString Selector = SelectorPrefix + TEXT("/") + EncodeSelectorSegment(Segments[0]);
    int32 Offset = 1;
    while (Offset < Segments.Num())
    {
        if (GameplayTagValueCodec::Classify(Property) != GameplayTagValueCodec::EPropertyKind::None)
        {
            goto NotFound;
        }
        if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            FProperty* Child = nullptr;
            for (TFieldIterator<FProperty> It(Struct->Struct, EFieldIterationFlags::IncludeSuper); It; ++It)
            {
                if (IsCandidate(*It, false) && AuthoredName(Struct->Struct, *It) == Segments[Offset])
                { Child = *It; break; }
            }
            if (Child == nullptr) goto NotFound;
            Selector += TEXT("/") + EncodeSelectorSegment(Segments[Offset++]);
            Address = Child->ContainerPtrToValuePtr<void>(Address);
            Property = Child;
            continue;
        }
        if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
        {
            FScriptArrayHelper Helper(Array, Address);
            int32 Index = INDEX_NONE;
            if (Offset + 1 >= Segments.Num() || Segments[Offset] != TEXT("items")
                || !ParseIndex(Segments[Offset + 1], Helper.Num(), Index)) goto NotFound;
            Selector += TEXT("/items/") + LexToString(Index);
            Offset += 2; Address = Helper.GetRawPtr(Index); Property = Array->Inner; continue;
        }
        if (const FSetProperty* Set = CastField<FSetProperty>(Property))
        {
            const TArray<int32> Indexes = SortedSetIndexes(Set, Address);
            int32 Index = INDEX_NONE;
            if (Offset + 1 >= Segments.Num() || Segments[Offset] != TEXT("items")
                || !ParseIndex(Segments[Offset + 1], Indexes.Num(), Index)) goto NotFound;
            FScriptSetHelper Helper(Set, Address);
            Selector += TEXT("/items/") + LexToString(Index);
            Offset += 2; Address = Helper.GetElementPtr(Indexes[Index]); Property = Set->ElementProp; continue;
        }
        if (const FMapProperty* Map = CastField<FMapProperty>(Property))
        {
            const TArray<int32> Indexes = SortedMapIndexes(Map, Address);
            int32 Index = INDEX_NONE;
            if (Offset + 2 >= Segments.Num() || Segments[Offset] != TEXT("entries")
                || !ParseIndex(Segments[Offset + 1], Indexes.Num(), Index)
                || (Segments[Offset + 2] != TEXT("key") && Segments[Offset + 2] != TEXT("value"))) goto NotFound;
            FScriptMapHelper Helper(Map, Address);
            const bool bKey = Segments[Offset + 2] == TEXT("key");
            Selector += TEXT("/entries/") + LexToString(Index) + (bKey ? TEXT("/key") : TEXT("/value"));
            Offset += 3;
            Address = bKey ? Helper.GetKeyPtr(Indexes[Index]) : Helper.GetValuePtr(Indexes[Index]);
            Property = bKey ? Map->KeyProp : Map->ValueProp;
            continue;
        }
        goto NotFound;
    }
    Out = {Property, Address, Selector};
    return true;

NotFound:
    OutError = {TEXT("not_found"), TEXT("The selected nested structured value was not found")};
    return false;
}

bool BuildCollectionPage(
    const FResolvedValue& Resolved,
    UObject* Owner,
    int32 PageIndex,
    int32 PageSize,
    const FString& Snapshot,
    TSharedPtr<FUnrealMCPRecord>& Out,
    FUnrealMCPError& OutError)
{
    int32 Total = 0;
    if (!CollectionCount(Resolved.Property, Resolved.Address, Total)) return false;
    if (Total > UnrealMCP::MaxGameDataRows)
    {
        OutError = {TEXT("data_limit_exceeded"), TEXT("The structured collection exceeds the scan limit")};
        return false;
    }
    const int64 Start64 = static_cast<int64>(PageIndex) * PageSize;
    const int32 Start = static_cast<int32>(FMath::Min<int64>(Start64, Total));
    const int32 End = FMath::Min(Start + PageSize, Total);
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetObjectField(TEXT("collection"), CollectionDescriptor(Resolved.Property, Total, Resolved.Selector));
    TArray<TSharedPtr<FUnrealMCPValue>> Values;
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Resolved.Property))
    {
        FScriptArrayHelper Helper(Array, Resolved.Address);
        for (int32 Index = Start; Index < End; ++Index)
        {
            const TSharedRef<FUnrealMCPRecord> Item = MakeShared<FUnrealMCPRecord>();
            Item->SetNumberField(TEXT("index"), Index);
            TSharedPtr<FUnrealMCPValue> Value; FUnrealMCPError Error;
            const FString Selector = Resolved.Selector + TEXT("/items/") + LexToString(Index);
            if (!SemanticValue(Array->Inner, Helper.GetRawPtr(Index), Owner, Selector, 1, Value, Error))
                Value = MakeShared<FUnrealMCPValueObject>(Limitation(Array->Inner, Error));
            Item->SetField(TEXT("value"), Value);
            Values.Add(MakeShared<FUnrealMCPValueObject>(Item));
        }
        Result->SetArrayField(TEXT("items"), Values);
    }
    else if (const FSetProperty* Set = CastField<FSetProperty>(Resolved.Property))
    {
        FScriptSetHelper Helper(Set, Resolved.Address);
        const TArray<int32> Indexes = SortedSetIndexes(Set, Resolved.Address);
        for (int32 Index = Start; Index < End; ++Index)
        {
            const TSharedRef<FUnrealMCPRecord> Item = MakeShared<FUnrealMCPRecord>();
            Item->SetNumberField(TEXT("index"), Index);
            TSharedPtr<FUnrealMCPValue> Value; FUnrealMCPError Error;
            const FString Selector = Resolved.Selector + TEXT("/items/") + LexToString(Index);
            if (!SemanticValue(Set->ElementProp, Helper.GetElementPtr(Indexes[Index]), Owner, Selector, 1, Value, Error))
                Value = MakeShared<FUnrealMCPValueObject>(Limitation(Set->ElementProp, Error));
            Item->SetField(TEXT("value"), Value);
            Values.Add(MakeShared<FUnrealMCPValueObject>(Item));
        }
        Result->SetArrayField(TEXT("items"), Values);
    }
    else if (const FMapProperty* Map = CastField<FMapProperty>(Resolved.Property))
    {
        FScriptMapHelper Helper(Map, Resolved.Address);
        const TArray<int32> Indexes = SortedMapIndexes(Map, Resolved.Address);
        for (int32 Index = Start; Index < End; ++Index)
        {
            const TSharedRef<FUnrealMCPRecord> Entry = MakeShared<FUnrealMCPRecord>();
            Entry->SetNumberField(TEXT("index"), Index);
            TSharedPtr<FUnrealMCPValue> Key; TSharedPtr<FUnrealMCPValue> Value; FUnrealMCPError Error;
            const int32 RawIndex = Indexes[Index];
            if (!SemanticValue(Map->KeyProp, Helper.GetKeyPtr(RawIndex), Owner,
                Resolved.Selector + TEXT("/entries/") + LexToString(Index) + TEXT("/key"), 1, Key, Error))
                Key = MakeShared<FUnrealMCPValueObject>(Limitation(Map->KeyProp, Error));
            Error = {};
            if (!SemanticValue(Map->ValueProp, Helper.GetValuePtr(RawIndex), Owner,
                Resolved.Selector + TEXT("/entries/") + LexToString(Index) + TEXT("/value"), 1, Value, Error))
                Value = MakeShared<FUnrealMCPValueObject>(Limitation(Map->ValueProp, Error));
            Entry->SetField(TEXT("key"), Key); Entry->SetField(TEXT("value"), Value);
            Values.Add(MakeShared<FUnrealMCPValueObject>(Entry));
        }
        Result->SetArrayField(TEXT("entries"), Values);
    }
    Result->SetObjectField(TEXT("page"), PageRecord(PageIndex, PageSize, Total, Values.Num(), Snapshot));
    Out = Result;
    return true;
}
}

FString UnrealMCP::StructuredDataInspection::EncodeSelectorSegment(const FString& Input)
{
    const FTCHARToUTF8 Encoded(*Input);
    FString Result;
    static const TCHAR Digits[] = TEXT("0123456789ABCDEF");
    for (int32 Index = 0; Index < Encoded.Length(); ++Index)
    {
        const uint8 Byte = static_cast<uint8>(Encoded.Get()[Index]);
        if (Private::IsUnreserved(Byte)) Result.AppendChar(static_cast<TCHAR>(Byte));
        else
        {
            Result.AppendChar('%'); Result.AppendChar(Digits[(Byte >> 4) & 0xF]); Result.AppendChar(Digits[Byte & 0xF]);
        }
    }
    return Result;
}

bool UnrealMCP::StructuredDataInspection::BuildPropertyPage(
    const FUnrealMCPStructuredDataSource& Source,
    const FString& SelectorPrefix,
    int32 PageIndex,
    int32 PageSize,
    const FString& SnapshotId,
    TSharedPtr<FUnrealMCPRecord>& OutProperties,
    FUnrealMCPError& OutError)
{
    const TArray<FProperty*> Values = Private::Properties(Source, OutError);
    if (!OutError.Code.IsEmpty()) return false;
    const int64 Start64 = static_cast<int64>(PageIndex) * PageSize;
    const int32 Start = static_cast<int32>(FMath::Min<int64>(Start64, Values.Num()));
    const int32 End = FMath::Min(Start + PageSize, Values.Num());
    TArray<TSharedPtr<FUnrealMCPValue>> Items;
    for (int32 Index = Start; Index < End; ++Index)
        Items.Add(MakeShared<FUnrealMCPValueObject>(Private::PropertyRecord(Source, Values[Index], SelectorPrefix)));
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetNumberField(TEXT("count"), Values.Num());
    Result->SetArrayField(TEXT("items"), Items);
    Result->SetObjectField(TEXT("page"), Private::PageRecord(PageIndex, PageSize, Values.Num(), Items.Num(), SnapshotId));
    OutProperties = Result;
    return true;
}

bool UnrealMCP::StructuredDataInspection::BuildFieldValues(
    const FUnrealMCPStructuredDataSource& Source,
    const FString& SelectorPrefix,
    TSharedPtr<FUnrealMCPRecord>& OutValues,
    FUnrealMCPError& OutError)
{
    const TArray<FProperty*> Values = Private::Properties(Source, OutError);
    if (!OutError.Code.IsEmpty()) return false;
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    for (FProperty* Property : Values)
    {
        const FString Name = Private::AuthoredName(Source.Type, Property);
        TSharedPtr<FUnrealMCPValue> Value; FUnrealMCPError Error;
        if (!Private::SemanticValue(Property, Property->ContainerPtrToValuePtr<void>(Source.Data), Source.Owner,
            SelectorPrefix + TEXT("/") + EncodeSelectorSegment(Name), 0, Value, Error))
            Value = MakeShared<FUnrealMCPValueObject>(Private::Limitation(Property, Error));
        Result->SetField(Name, Value);
    }
    OutValues = Result;
    return true;
}

bool UnrealMCP::StructuredDataInspection::InspectField(
    const FUnrealMCPStructuredDataSource& Source,
    const FString& SelectorPrefix,
    const TArray<FString>& FieldSegments,
    const FString& CanonicalSelector,
    int32 PageIndex,
    int32 PageSize,
    bool bHasPaging,
    const FString& SnapshotId,
    TSharedPtr<FUnrealMCPRecord>& OutInspection,
    FUnrealMCPError& OutError)
{
    Private::FResolvedValue Resolved;
    if (!Private::Resolve(Source, SelectorPrefix, FieldSegments, Resolved, OutError)) return false;
    int32 Count = 0;
    if (Private::CollectionCount(Resolved.Property, Resolved.Address, Count))
    {
        return Private::BuildCollectionPage(Resolved, Source.Owner, PageIndex, PageSize, SnapshotId, OutInspection, OutError);
    }
    if (bHasPaging)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Paging parameters require a collection selector")};
        return false;
    }
    TSharedPtr<FUnrealMCPValue> Value; FUnrealMCPError Error;
    if (!Private::SemanticValue(Resolved.Property, Resolved.Address, Source.Owner, Resolved.Selector, 0, Value, Error))
        Value = MakeShared<FUnrealMCPValueObject>(Private::Limitation(Resolved.Property, Error));
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetField(TEXT("value"), Value);
    OutInspection = Result;
    return true;
}

FString UnrealMCP::StructuredDataInspection::BuildSnapshot(
    const FUnrealMCPStructuredDataSource& Source,
    const FString& Identity)
{
    FUnrealMCPError Error;
    const TArray<FProperty*> Values = Private::Properties(Source, Error);
    if (!Error.Code.IsEmpty()) return FString();
    TArray<FString> Lines;
    Lines.Add(TEXT("identity|") + Identity);
    Lines.Add(TEXT("type|") + (Source.Type != nullptr ? Source.Type->GetPathName() : FString()));
    for (FProperty* Property : Values)
    {
        const void* Address = Property->ContainerPtrToValuePtr<void>(Source.Data);
        Lines.Add(Private::DeclaredBy(Property) + TEXT("|") + Private::AuthoredName(Source.Type, Property)
            + TEXT("|") + Property->GetClass()->GetName() + TEXT("|") + Private::Sha1(Private::Canonical(Property, Address)));
    }
    Lines.Sort();
    return Private::Sha1(FString::Join(Lines, TEXT("\n")));
}
