#pragma once

#include "CoreMinimal.h"

class FProperty;
class FUnrealMCPValue;
struct FUnrealMCPError;

namespace UnrealMCP::GameplayTagValueCodec
{
enum class EPropertyKind : uint8
{
    None,
    Tag,
    Container,
};

EPropertyKind Classify(const FProperty* Property);
const TCHAR* TypeName(EPropertyKind Kind);
bool Encode(const FProperty* Property, const void* Value, TSharedPtr<FUnrealMCPValue>& OutValue,
    FUnrealMCPError& OutError);
bool Decode(const FProperty* Property, void* Value, const TSharedPtr<FUnrealMCPValue>& Input,
    const TCHAR* InvalidCode, FUnrealMCPError& OutError);
}
