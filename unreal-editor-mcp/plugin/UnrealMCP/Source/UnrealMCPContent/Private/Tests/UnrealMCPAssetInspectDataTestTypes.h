#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "UnrealMCPAssetInspectDataTestTypes.generated.h"

UCLASS(EditInlineNew, DefaultToInstanced)
class UUnrealMCPAssetInspectInlineFixture : public UObject
{
    GENERATED_BODY()
};

UCLASS()
class UUnrealMCPAssetInspectDataFixture : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, Category = "Asset Inspect Data Fixture")
    int32 Damage = 35;

    UPROPERTY(EditAnywhere, Category = "Asset Inspect Data Fixture")
    TArray<FName> Tags;

    UPROPERTY(EditAnywhere, Instanced, Category = "Asset Inspect Data Fixture")
    TObjectPtr<UUnrealMCPAssetInspectInlineFixture> UnsupportedSubobject;
};

USTRUCT()
struct FUnrealMCPAssetInspectDataRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Asset Inspect Data Fixture")
    int32 Damage = 0;

    UPROPERTY(EditAnywhere, Category = "Asset Inspect Data Fixture")
    TArray<FName> Tags;

    UPROPERTY(EditAnywhere, Category = "Asset Inspect Data Fixture")
    TMap<FName, float> Multipliers;
};

USTRUCT()
struct FUnrealMCPGameplayTagNestedRowValue
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Gameplay Tag Data Fixture")
    FGameplayTag Tag;

    UPROPERTY(EditAnywhere, Category = "Gameplay Tag Data Fixture")
    FGameplayTagContainer Tags;
};

USTRUCT()
struct FUnrealMCPGameplayTagDataRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Gameplay Tag Data Fixture")
    FGameplayTag Tag;

    UPROPERTY(EditAnywhere, Category = "Gameplay Tag Data Fixture")
    FGameplayTagContainer Tags;

    UPROPERTY(EditAnywhere, Category = "Gameplay Tag Data Fixture")
    FUnrealMCPGameplayTagNestedRowValue Nested;
};
