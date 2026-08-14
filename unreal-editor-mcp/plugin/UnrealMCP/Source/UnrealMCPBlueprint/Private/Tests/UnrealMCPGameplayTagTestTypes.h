#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "UnrealMCPGameplayTagTestTypes.generated.h"

UCLASS()
class AUnrealMCPGameplayTagTestActor : public AActor
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Unreal MCP Gameplay Tag Test")
    FGameplayTag SingleTag;

    UPROPERTY(EditAnywhere, Category = "Unreal MCP Gameplay Tag Test")
    FGameplayTagContainer TagContainer;
};

UCLASS(ClassGroup = "Unreal MCP", meta = (BlueprintSpawnableComponent))
class UUnrealMCPGameplayTagTestComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Unreal MCP Gameplay Tag Test")
    FGameplayTag SingleTag;

    UPROPERTY(EditAnywhere, Category = "Unreal MCP Gameplay Tag Test")
    FGameplayTagContainer TagContainer;
};
