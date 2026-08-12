#pragma once

#include "CoreMinimal.h"

class FUnrealMCPRecord;

enum class EUnrealMCPValueType : uint8
{
    Null,
    String,
    Number,
    Boolean,
    Array,
    Record,
};

class FUnrealMCPValue
{
public:
    explicit FUnrealMCPValue(EUnrealMCPValueType InType) : Type(InType) {}
    virtual ~FUnrealMCPValue() = default;

    virtual FString AsString() const { return FString(); }
    virtual double AsNumber() const { return 0.0; }
    virtual bool AsBool() const { return false; }
    virtual const TArray<TSharedPtr<FUnrealMCPValue>>& AsArray() const;
    virtual TSharedPtr<FUnrealMCPRecord> AsObject() const { return nullptr; }

    virtual bool TryGetString(FString& OutValue) const { return false; }
    virtual bool TryGetNumber(double& OutValue) const { return false; }
    virtual bool TryGetBool(bool& OutValue) const { return false; }
    virtual bool TryGetObject(const TSharedPtr<FUnrealMCPRecord>*& OutValue) const
    {
        OutValue = nullptr;
        return false;
    }
    virtual bool TryGetArray(const TArray<TSharedPtr<FUnrealMCPValue>>*& OutValue) const
    {
        OutValue = nullptr;
        return false;
    }

    static bool CompareEqual(const FUnrealMCPValue& Left, const FUnrealMCPValue& Right);

    EUnrealMCPValueType Type;
};

class FUnrealMCPValueNull final : public FUnrealMCPValue
{
public:
    FUnrealMCPValueNull() : FUnrealMCPValue(EUnrealMCPValueType::Null) {}
};

class FUnrealMCPValueString final : public FUnrealMCPValue
{
public:
    explicit FUnrealMCPValueString(FString InValue)
        : FUnrealMCPValue(EUnrealMCPValueType::String), Value(MoveTemp(InValue)) {}
    FString AsString() const override { return Value; }
    bool TryGetString(FString& OutValue) const override { OutValue = Value; return true; }
private:
    FString Value;
};

class FUnrealMCPValueNumber final : public FUnrealMCPValue
{
public:
    explicit FUnrealMCPValueNumber(double InValue)
        : FUnrealMCPValue(EUnrealMCPValueType::Number), Value(InValue) {}
    double AsNumber() const override { return Value; }
    bool TryGetNumber(double& OutValue) const override { OutValue = Value; return true; }
private:
    double Value;
};

class FUnrealMCPValueBoolean final : public FUnrealMCPValue
{
public:
    explicit FUnrealMCPValueBoolean(bool bInValue)
        : FUnrealMCPValue(EUnrealMCPValueType::Boolean), bValue(bInValue) {}
    bool AsBool() const override { return bValue; }
    bool TryGetBool(bool& OutValue) const override { OutValue = bValue; return true; }
private:
    bool bValue;
};

class FUnrealMCPValueArray final : public FUnrealMCPValue
{
public:
    explicit FUnrealMCPValueArray(TArray<TSharedPtr<FUnrealMCPValue>> InValue)
        : FUnrealMCPValue(EUnrealMCPValueType::Array), Value(MoveTemp(InValue)) {}
    const TArray<TSharedPtr<FUnrealMCPValue>>& AsArray() const override { return Value; }
    bool TryGetArray(const TArray<TSharedPtr<FUnrealMCPValue>>*& OutValue) const override
    {
        OutValue = &Value;
        return true;
    }
private:
    TArray<TSharedPtr<FUnrealMCPValue>> Value;
};

class FUnrealMCPValueObject final : public FUnrealMCPValue
{
public:
    explicit FUnrealMCPValueObject(TSharedPtr<FUnrealMCPRecord> InValue)
        : FUnrealMCPValue(EUnrealMCPValueType::Record), Value(MoveTemp(InValue)) {}
    TSharedPtr<FUnrealMCPRecord> AsObject() const override { return Value; }
    bool TryGetObject(const TSharedPtr<FUnrealMCPRecord>*& OutValue) const override
    {
        OutValue = &Value;
        return Value.IsValid();
    }
private:
    TSharedPtr<FUnrealMCPRecord> Value;
};

class FUnrealMCPRecord
{
public:
    void SetField(const FString& Name, TSharedPtr<FUnrealMCPValue> Value) { Values.Add(Name, MoveTemp(Value)); }
    void SetStringField(const FString& Name, const FString& Value) { SetField(Name, MakeShared<FUnrealMCPValueString>(Value)); }
    void SetNumberField(const FString& Name, double Value) { SetField(Name, MakeShared<FUnrealMCPValueNumber>(Value)); }
    void SetBoolField(const FString& Name, bool bValue) { SetField(Name, MakeShared<FUnrealMCPValueBoolean>(bValue)); }
    void SetArrayField(const FString& Name, const TArray<TSharedPtr<FUnrealMCPValue>>& Value)
    {
        SetField(Name, MakeShared<FUnrealMCPValueArray>(Value));
    }
    void SetArrayField(const FString& Name, TArray<TSharedPtr<FUnrealMCPValue>>&& Value)
    {
        SetField(Name, MakeShared<FUnrealMCPValueArray>(MoveTemp(Value)));
    }
    void SetObjectField(const FString& Name, const TSharedPtr<FUnrealMCPRecord>& Value)
    {
        SetField(Name, MakeShared<FUnrealMCPValueObject>(Value));
    }

    bool HasField(const FString& Name) const { return Values.Contains(Name); }
    void RemoveField(const FString& Name) { Values.Remove(Name); }
    TSharedPtr<FUnrealMCPValue> TryGetField(const FString& Name) const
    {
        const TSharedPtr<FUnrealMCPValue>* Value = Values.Find(Name);
        return Value != nullptr ? *Value : nullptr;
    }
    bool TryGetStringField(const FString& Name, FString& OutValue) const
    {
        const TSharedPtr<FUnrealMCPValue> Value = TryGetField(Name);
        return Value.IsValid() && Value->TryGetString(OutValue);
    }
    bool TryGetNumberField(const FString& Name, double& OutValue) const
    {
        const TSharedPtr<FUnrealMCPValue> Value = TryGetField(Name);
        return Value.IsValid() && Value->TryGetNumber(OutValue);
    }
    bool TryGetBoolField(const FString& Name, bool& OutValue) const
    {
        const TSharedPtr<FUnrealMCPValue> Value = TryGetField(Name);
        return Value.IsValid() && Value->TryGetBool(OutValue);
    }
    bool TryGetArrayField(const FString& Name, const TArray<TSharedPtr<FUnrealMCPValue>>*& OutValue) const
    {
        const TSharedPtr<FUnrealMCPValue> Value = TryGetField(Name);
        if (!Value.IsValid() || Value->Type != EUnrealMCPValueType::Array) { OutValue = nullptr; return false; }
        OutValue = &Value->AsArray();
        return true;
    }
    bool TryGetObjectField(const FString& Name, const TSharedPtr<FUnrealMCPRecord>*& OutValue) const
    {
        const TSharedPtr<FUnrealMCPValue> Value = TryGetField(Name);
        return Value.IsValid() && Value->TryGetObject(OutValue);
    }

    FString GetStringField(const FString& Name) const { FString Value; TryGetStringField(Name, Value); return Value; }
    double GetNumberField(const FString& Name) const { double Value = 0.0; TryGetNumberField(Name, Value); return Value; }
    int32 GetIntegerField(const FString& Name) const { return static_cast<int32>(GetNumberField(Name)); }
    bool GetBoolField(const FString& Name) const { bool bValue = false; TryGetBoolField(Name, bValue); return bValue; }
    const TArray<TSharedPtr<FUnrealMCPValue>>& GetArrayField(const FString& Name) const;
    TSharedPtr<FUnrealMCPRecord> GetObjectField(const FString& Name) const;

    template<EUnrealMCPValueType ExpectedType>
    bool HasTypedField(const FString& Name) const
    {
        const TSharedPtr<FUnrealMCPValue> Value = TryGetField(Name);
        return Value.IsValid() && Value->Type == ExpectedType;
    }

    TMap<FString, TSharedPtr<FUnrealMCPValue>> Values;
};

struct FUnrealMCPCommandRequest
{
    FString Command;
    TSharedPtr<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
};

struct FUnrealMCPResultRecord
{
    TSharedPtr<FUnrealMCPRecord> Data = MakeShared<FUnrealMCPRecord>();
};

struct FUnrealMCPError
{
    FString Code;
    FString Message;
    TSharedPtr<FUnrealMCPRecord> Details = MakeShared<FUnrealMCPRecord>();
    bool bRetryable = false;
};

struct FUnrealMCPCapabilityRecord { FString Name; bool bAvailable = false; TMap<FString, int32> Limits; };
struct FUnrealMCPIdentityRecord { FString Id; FString Kind; FString Path; FString Snapshot; };
struct FUnrealMCPSelectorRecord { FString Selector; TArray<FString> Segments; };
struct FUnrealMCPPagingRecord { int32 Offset = 0; int32 Limit = 0; FString Cursor; FString NextCursor; bool bTruncated = false; };
struct FUnrealMCPDiagnosticRecord { FString Code; FString Message; FString Severity; };
struct FUnrealMCPMutationRecord { FString OperationId; FString OperationState; FString BridgeInstanceId; FString RequestDigest; };
struct FUnrealMCPPersistenceRecord { FString PackagePath; bool bSaved = false; bool bVerified = false; };
