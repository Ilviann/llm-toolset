#include "UnrealMCPWireTypes.h"

namespace
{
const TArray<TSharedPtr<FUnrealMCPValue>>& EmptyArray()
{
    static const TArray<TSharedPtr<FUnrealMCPValue>> Value;
    return Value;
}
}

const TArray<TSharedPtr<FUnrealMCPValue>>& FUnrealMCPValue::AsArray() const
{
    return EmptyArray();
}

const TArray<TSharedPtr<FUnrealMCPValue>>& FUnrealMCPRecord::GetArrayField(const FString& Name) const
{
    const TArray<TSharedPtr<FUnrealMCPValue>>* Value = nullptr;
    return TryGetArrayField(Name, Value) && Value != nullptr ? *Value : EmptyArray();
}

TSharedPtr<FUnrealMCPRecord> FUnrealMCPRecord::GetObjectField(const FString& Name) const
{
    const TSharedPtr<FUnrealMCPRecord>* Value = nullptr;
    return TryGetObjectField(Name, Value) && Value != nullptr ? *Value : nullptr;
}

bool FUnrealMCPValue::CompareEqual(const FUnrealMCPValue& Left, const FUnrealMCPValue& Right)
{
    if (Left.Type != Right.Type) return false;
    switch (Left.Type)
    {
    case EUnrealMCPValueType::Null: return true;
    case EUnrealMCPValueType::String: return Left.AsString() == Right.AsString();
    case EUnrealMCPValueType::Number: return Left.AsNumber() == Right.AsNumber();
    case EUnrealMCPValueType::Boolean: return Left.AsBool() == Right.AsBool();
    case EUnrealMCPValueType::Array:
    {
        const auto& A = Left.AsArray(); const auto& B = Right.AsArray();
        if (A.Num() != B.Num()) return false;
        for (int32 Index = 0; Index < A.Num(); ++Index)
        {
            if (A[Index].IsValid() != B[Index].IsValid()
                || (A[Index].IsValid() && !CompareEqual(*A[Index], *B[Index]))) return false;
        }
        return true;
    }
    case EUnrealMCPValueType::Record:
    {
        const auto A = Left.AsObject(); const auto B = Right.AsObject();
        if (A.IsValid() != B.IsValid()) return false;
        if (!A.IsValid()) return true;
        if (A->Values.Num() != B->Values.Num()) return false;
        for (const auto& Pair : A->Values)
        {
            const TSharedPtr<FUnrealMCPValue>* Other = B->Values.Find(Pair.Key);
            if (Other == nullptr || Pair.Value.IsValid() != Other->IsValid()
                || (Pair.Value.IsValid() && !CompareEqual(*Pair.Value, **Other))) return false;
        }
        return true;
    }
    }
    return false;
}
