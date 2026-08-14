#include "UnrealMCPJsonCodec.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMCPVersion.h"

namespace
{
constexpr int32 MaxFieldsPerRecord = 4096;
constexpr int32 MaxItemsPerArray = 4096;
constexpr int32 MaxTotalValues = 32768;

bool DecodeValueBounded(
    const TSharedPtr<FJsonValue>& Input,
    TSharedPtr<FUnrealMCPValue>& OutValue,
    FUnrealMCPError& OutError,
    int32 Depth,
    int32& InOutValues)
{
    if (!Input.IsValid() || Depth > UnrealMCP::MaxJsonDepth || ++InOutValues > MaxTotalValues)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Request exceeds native wire value limits")};
        return false;
    }
    switch (Input->Type)
    {
    case EJson::Null:
        OutValue = MakeShared<FUnrealMCPValueNull>();
        return true;
    case EJson::String:
    {
        FString Value;
        if (!Input->TryGetString(Value) || Value.Len() > UnrealMCP::MaxStringLength)
        {
            OutError = {TEXT("invalid_argument"), TEXT("Request contains an invalid or oversized string")};
            return false;
        }
        OutValue = MakeShared<FUnrealMCPValueString>(MoveTemp(Value));
        return true;
    }
    case EJson::Number:
    {
        double Value = 0.0;
        if (!Input->TryGetNumber(Value) || !FMath::IsFinite(Value))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Request contains a non-finite number")};
            return false;
        }
        OutValue = MakeShared<FUnrealMCPValueNumber>(Value);
        return true;
    }
    case EJson::Boolean:
    {
        bool bValue = false;
        if (!Input->TryGetBool(bValue)) return false;
        OutValue = MakeShared<FUnrealMCPValueBoolean>(bValue);
        return true;
    }
    case EJson::Array:
    {
        const TArray<TSharedPtr<FJsonValue>>& InputItems = Input->AsArray();
        if (InputItems.Num() > MaxItemsPerArray)
        {
            OutError = {TEXT("invalid_argument"), TEXT("Request array exceeds the native wire item limit")};
            return false;
        }
        TArray<TSharedPtr<FUnrealMCPValue>> Items;
        Items.Reserve(InputItems.Num());
        for (const TSharedPtr<FJsonValue>& Item : InputItems)
        {
            TSharedPtr<FUnrealMCPValue> Decoded;
            if (!DecodeValueBounded(Item, Decoded, OutError, Depth + 1, InOutValues)) return false;
            Items.Add(MoveTemp(Decoded));
        }
        OutValue = MakeShared<FUnrealMCPValueArray>(MoveTemp(Items));
        return true;
    }
    case EJson::Object:
    {
        const TSharedPtr<FJsonObject> Object = Input->AsObject();
        if (!Object.IsValid() || Object->Values.Num() > MaxFieldsPerRecord)
        {
            OutError = {TEXT("invalid_argument"), TEXT("Request record exceeds the native wire field limit")};
            return false;
        }
        const TSharedRef<FUnrealMCPRecord> Record = MakeShared<FUnrealMCPRecord>();
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
        {
            if (Pair.Key.IsEmpty() || Pair.Key.Len() > UnrealMCP::MaxStringLength)
            {
                OutError = {TEXT("invalid_argument"), TEXT("Request contains an invalid record field name")};
                return false;
            }
            TSharedPtr<FUnrealMCPValue> Decoded;
            if (!DecodeValueBounded(Pair.Value, Decoded, OutError, Depth + 1, InOutValues)) return false;
            Record->SetField(Pair.Key, MoveTemp(Decoded));
        }
        OutValue = MakeShared<FUnrealMCPValueObject>(Record);
        return true;
    }
    default:
        OutError = {TEXT("invalid_argument"), TEXT("Request contains an unsupported native wire value")};
        return false;
    }
}

TSharedPtr<FJsonValue> EncodeValueImpl(const TSharedPtr<FUnrealMCPValue>& Input)
{
    if (!Input.IsValid()) return MakeShared<FJsonValueNull>();
    switch (Input->Type)
    {
    case EUnrealMCPValueType::Null: return MakeShared<FJsonValueNull>();
    case EUnrealMCPValueType::String: return MakeShared<FJsonValueString>(Input->AsString());
    case EUnrealMCPValueType::Number: return MakeShared<FJsonValueNumber>(Input->AsNumber());
    case EUnrealMCPValueType::Boolean: return MakeShared<FJsonValueBoolean>(Input->AsBool());
    case EUnrealMCPValueType::Array:
    {
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Reserve(Input->AsArray().Num());
        for (const TSharedPtr<FUnrealMCPValue>& Value : Input->AsArray()) Values.Add(EncodeValueImpl(Value));
        return MakeShared<FJsonValueArray>(MoveTemp(Values));
    }
    case EUnrealMCPValueType::Record:
    {
        const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        const TSharedPtr<FUnrealMCPRecord> Record = Input->AsObject();
        if (Record.IsValid())
        {
            for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Pair : Record->Values)
                Result->SetField(Pair.Key, EncodeValueImpl(Pair.Value));
        }
        return MakeShared<FJsonValueObject>(Result);
    }
    }
    return MakeShared<FJsonValueNull>();
}
}

bool UnrealMCP::JsonCodec::DecodeValue(
    const TSharedPtr<FJsonValue>& Input,
    TSharedPtr<FUnrealMCPValue>& OutValue,
    FUnrealMCPError& OutError)
{
    int32 Values = 0;
    return DecodeValueBounded(Input, OutValue, OutError, 0, Values);
}

bool UnrealMCP::JsonCodec::DecodeRecord(
    const TSharedPtr<FJsonObject>& Input,
    TSharedPtr<FUnrealMCPRecord>& OutRecord,
    FUnrealMCPError& OutError)
{
    if (!Input.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("Request arguments must be a record")};
        return false;
    }
    TSharedPtr<FUnrealMCPValue> Value;
    if (!DecodeValue(MakeShared<FJsonValueObject>(Input), Value, OutError)) return false;
    OutRecord = Value->AsObject();
    return OutRecord.IsValid();
}

TSharedPtr<FJsonValue> UnrealMCP::JsonCodec::EncodeValue(const TSharedPtr<FUnrealMCPValue>& Input)
{
    return EncodeValueImpl(Input);
}

TSharedPtr<FJsonObject> UnrealMCP::JsonCodec::EncodeRecord(const TSharedPtr<FUnrealMCPRecord>& Input)
{
    return EncodeValueImpl(MakeShared<FUnrealMCPValueObject>(Input))->AsObject();
}

bool UnrealMCP::JsonCodec::Serialize(const TSharedPtr<FUnrealMCPRecord>& Input, FString& OutText)
{
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutText);
    return FJsonSerializer::Serialize(EncodeRecord(Input).ToSharedRef(), Writer);
}

bool UnrealMCP::JsonCodec::SerializeValue(const TSharedPtr<FUnrealMCPValue>& Input, FString& OutText)
{
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutText);
    return FJsonSerializer::Serialize(EncodeValue(Input).ToSharedRef(), TEXT(""), Writer);
}
