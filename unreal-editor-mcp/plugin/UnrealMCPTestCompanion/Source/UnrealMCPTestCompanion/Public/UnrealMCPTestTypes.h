#pragma once

#include "AttributeSet.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
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

UENUM(BlueprintType)
enum class EUnrealMCPInspectionState : uint8
{
    Unknown,
    Ready,
};

USTRUCT(BlueprintType)
struct UNREALMCPTESTCOMPANION_API FUnrealMCPNestedInspectionValue
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    int32 Count = 7;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FText Label = FText::FromString(TEXT("Nested"));
};

UCLASS(EditInlineNew, DefaultToInstanced)
class UNREALMCPTESTCOMPANION_API UUnrealMCPInlineInspectionObject : public UObject
{
    GENERATED_BODY()
};

USTRUCT(BlueprintType)
struct UNREALMCPTESTCOMPANION_API FUnrealMCPInspectionRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FGameplayTag Tag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FGameplayTagContainer Tags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FGuid Id;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FText Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    EUnrealMCPInspectionState State = EUnrealMCPInspectionState::Ready;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    TSoftObjectPtr<UDataTable> Table;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    TSoftClassPtr<UObject> Class;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    TArray<TSoftObjectPtr<UDataTable>> Tables;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FUnrealMCPNestedInspectionValue Nested;

    UPROPERTY(EditAnywhere, Instanced, Category = "Unreal MCP Test")
    TObjectPtr<UUnrealMCPInlineInspectionObject> UnsupportedObject;
};

UCLASS(Blueprintable, ClassGroup = "Unreal MCP Test", meta = (BlueprintSpawnableComponent))
class UNREALMCPTESTCOMPANION_API UUnrealMCPInspectionComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FGameplayTag Tag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FGameplayTagContainer Tags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FGuid Id;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FText Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    EUnrealMCPInspectionState State = EUnrealMCPInspectionState::Ready;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    TSoftObjectPtr<UObject> Asset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    TSoftClassPtr<UObject> Class;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    TArray<TSoftObjectPtr<UObject>> Assets;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FUnrealMCPNestedInspectionValue Nested;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    TMap<FGameplayTag, int32> Capacities;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FGameplayAttribute ObservedAttribute;
};

UCLASS(BlueprintType)
class UNREALMCPTESTCOMPANION_API UUnrealMCPInspectionDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FGuid Id;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    FUnrealMCPInspectionRow Value;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    TObjectPtr<UDataTable> ReferencedTable;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    TSoftObjectPtr<UDataTable> SoftTable;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    TArray<TSoftObjectPtr<UDataTable>> SoftTables;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Unreal MCP Test")
    TMap<FGameplayAttribute, float> GrantedAttributes;

    UPROPERTY(EditAnywhere, Instanced, Category = "Unreal MCP Test")
    TObjectPtr<UUnrealMCPInlineInspectionObject> UnsupportedObject;
};
