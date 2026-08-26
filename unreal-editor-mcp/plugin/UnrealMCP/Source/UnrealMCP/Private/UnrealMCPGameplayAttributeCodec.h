#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UObject;

namespace UnrealMCP::GameplayAttributeCodec
{
bool IsType(const UObject* TypeObject);
bool Encode(const FString& Text, TSharedPtr<FJsonObject>& OutValue);
}
