#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UnrealMCPTestTypes.generated.h"

UCLASS(BlueprintType)
class UNREALMCPTESTCOMPANION_API UUnrealMCPTestAsset : public UObject
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Unreal MCP Test")
    int32 Value = 0;
};

UCLASS(Blueprintable, ClassGroup = "Unreal MCP Test", meta = (BlueprintSpawnableComponent))
class UNREALMCPTESTCOMPANION_API UUnrealMCPTestComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    int32 Value = 0;
};
