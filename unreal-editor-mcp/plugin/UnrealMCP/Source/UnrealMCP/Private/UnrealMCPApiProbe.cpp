// This translation unit intentionally includes every public Unreal API family
// needed by the roadmap before bridge contracts are frozen. Keeping it in the
// normal module build makes every supported engine compile the probe.
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintActionFilter.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintEventNodeSpawner.h"
#include "BlueprintVariableNodeSpawner.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/ActorComponent.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/AssetManager.h"
#include "FileHelpers.h"
#include "Factories/WorldFactory.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Actor.h"
#include "GameMapsSettings.h"
#include "GameFramework/GameState.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_Tunnel.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "SubobjectDataSubsystem.h"
#include "DataTableEditorUtils.h"
#include "Factories/DataTableFactory.h"
#include "Kismet2/StructureEditorUtils.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintOperationUtils.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "WorldPartition/WorldPartitionHelpers.h"
#include "IUnrealMCPModule.h"
#include "UnrealMCPCompanionApi.h"
#include "UnrealMCPAssetAuthoringKernel.h"
#include "UnrealMCPAssetFamilyRegistry.h"

namespace UnrealMCP::ApiProbe
{
void RequireLevelInspectionApis(
    UWorldPartition* WorldPartition,
    FWorldPartitionActorDescInstance* Descriptor,
    AActor* Actor)
{
    FWorldPartitionHelpers::ForEachActorDescInstance(
        WorldPartition,
        [](const FWorldPartitionActorDescInstance*) { return false; });
    const FGuid Guid = Descriptor->GetGuid();
    (void)Descriptor->GetActor(false, false);
    (void)Descriptor->GetEditorBounds();
    (void)Descriptor->GetDataLayers();
    FWorldPartitionReference Reference(WorldPartition, Guid);
    (void)Reference.GetActor();
    TArray<UActorComponent*> Components;
    Actor->GetComponents(Components);
}

void RequirePublicTypes()
{
    static_assert(sizeof(FScopedTransaction) > 0);
    static_assert(sizeof(FCompilerResultsLog) > 0);
    static_assert(TIsDerivedFrom<UEdGraphSchema_K2, UEdGraphSchema>::Value);
    static_assert(TIsDerivedFrom<UBlueprintNodeSpawner, UObject>::Value);
    static_assert(TIsDerivedFrom<UBlueprintFunctionNodeSpawner, UBlueprintNodeSpawner>::Value);
    static_assert(TIsDerivedFrom<UBlueprintEventNodeSpawner, UBlueprintNodeSpawner>::Value);
    static_assert(TIsDerivedFrom<UBlueprintVariableNodeSpawner, UBlueprintNodeSpawner>::Value);
    static_assert(TIsDerivedFrom<UK2Node_FunctionEntry, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_FunctionResult, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_CustomEvent, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_DynamicCast, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_IfThenElse, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_EnumLiteral, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_PromotableOperator, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_MacroInstance, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_Tunnel, UK2Node>::Value);
    static_assert(TIsDerivedFrom<USubobjectDataSubsystem, UEngineSubsystem>::Value);
    static_assert(TIsDerivedFrom<AGameModeBase, AActor>::Value);
    static_assert(TIsDerivedFrom<AGameMode, AGameModeBase>::Value);
    static_assert(TIsDerivedFrom<UGameMapsSettings, UObject>::Value);
    static_assert(TIsDerivedFrom<UUserDefinedStruct, UScriptStruct>::Value);
    static_assert(TIsDerivedFrom<UDataTableFactory, UFactory>::Value);
    static_assert(TIsDerivedFrom<UWorldFactory, UFactory>::Value);
    static_assert(TIsDerivedFrom<AGameStateBase, AActor>::Value);
    static_assert(TIsDerivedFrom<AGameState, AGameStateBase>::Value);
    static_assert(TIsDerivedFrom<UUserWidget, UWidget>::Value);
    static_assert(TIsDerivedFrom<UWidgetBlueprint, UBlueprint>::Value);
    static_assert(TIsDerivedFrom<UActorComponent, UObject>::Value);
    using FFindWidgetByName = UWidget* (UWidgetTree::*)(const FName&) const;
    (void)static_cast<FFindWidgetByName>(&UWidgetTree::FindWidget);
    (void)&UWidgetTree::RemoveWidget;
    (void)&FWidgetBlueprintOperationUtils::CreateWidgetBlueprint;
    (void)&FWidgetBlueprintOperationUtils::AddWidget;
    (void)&FWidgetBlueprintOperationUtils::MoveWidget;
    (void)&FWidgetBlueprintOperationUtils::RemoveWidget;
    (void)&FWidgetBlueprintOperationUtils::RenameWidget;
    (void)&FWidgetBlueprintOperationUtils::ToggleWidgetAsVariable;
    (void)&FHttpServerModule::IsAvailable;
    (void)&FAssetRegistryModule::GetRegistry;
    using FEnumerateWorldAssets = bool (IAssetRegistry::*)(
        const FARFilter&,
        TFunctionRef<bool(const FAssetData&)>,
        UE::AssetRegistry::EEnumerateAssetsFlags) const;
    (void)static_cast<FEnumerateWorldAssets>(&IAssetRegistry::EnumerateAssets);
    using FGetAssetReferencers = bool (IAssetRegistry::*)(
        const FAssetIdentifier&,
        TArray<FAssetDependency>&,
        UE::AssetRegistry::EDependencyCategory,
        const UE::AssetRegistry::FDependencyQuery&) const;
    (void)static_cast<FGetAssetReferencers>(&IAssetRegistry::GetReferencers);
    using FGetPackageAssets = bool (IAssetRegistry::*)(
        FName, TArray<FAssetData>&, bool, bool) const;
    (void)static_cast<FGetPackageAssets>(&IAssetRegistry::GetAssetsByPackageName);
    (void)&IAssetRegistry::IsGathering;
    (void)&IAssetRegistry::OnAssetAdded;
    (void)&IAssetRegistry::OnAssetRemoved;
    (void)&IAssetRegistry::OnAssetRenamed;
    (void)&IAssetRegistry::OnAssetUpdated;
    (void)&IAssetRegistry::OnFilesLoaded;
    (void)&UAssetEditorSubsystem::FindEditorsForAsset;
    (void)&UAssetEditorSubsystem::GetAllEditedAssets;
    (void)&FReferenceFinder::FindReferences;
    (void)&UAssetManager::GetPrimaryAssetPath;
    (void)&ObjectTools::GatherObjectReferencersForDeletion;
    using FDeleteSingleObject = bool (*)(UObject*, bool);
    (void)static_cast<FDeleteSingleObject>(&ObjectTools::DeleteSingleObject);
    using FDeleteAssets = int32 (*)(const TArray<FAssetData>&, bool);
    (void)static_cast<FDeleteAssets>(&ObjectTools::DeleteAssets);
    using FCleanupAfterDelete = void (*)(const TArray<UPackage*>&, bool);
    (void)static_cast<FCleanupAfterDelete>(&ObjectTools::CleanupAfterSuccessfulDelete);
    static_assert(sizeof(FThreadSafeObjectIterator) > 0);
    static_assert(sizeof(FUnrealMCPCompanionRegistration) > 0);
    static_assert(sizeof(FUnrealMCPAssetFamilyDescriptor) > 0);
    static_assert(sizeof(FUnrealMCPAssetFamilyInspectionContext) > 0);
    static_assert(sizeof(FUnrealMCPAssetFamilyCreationContext) > 0);
    static_assert(sizeof(FUnrealMCPAssetFamilyEditContext) > 0);
    static_assert(sizeof(FUnrealMCPAssetCreationRequest) > 0);
    static_assert(sizeof(FUnrealMCPAssetEditRequest) > 0);
    static_assert(TIsDerivedFrom<IUnrealMCPExtensionHandler, IUnrealMCPExtensionHandler>::Value);
    (void)&FBlueprintActionDatabase::Get;
    (void)&UEdGraphSchema_K2::CanCreateConnection;
    (void)&UEdGraphSchema_K2::TryCreateConnection;
    (void)&UEdGraphSchema_K2::CreateAutomaticConversionNodeAndConnections;
    (void)&UEdGraphSchema_K2::CreatePromotedConnection;
    (void)&UEdGraphSchema_K2::TrySetDefaultValue;
    (void)&UEdGraphSchema_K2::BreakSinglePinLink;
    using FSaveDirtyPackages = bool (*)(
        bool,
        bool,
        bool,
        bool,
        bool,
        bool,
        bool*,
        const FEditorFileUtils::FShouldIgnorePackageFunctionRef&,
        bool);
    (void)static_cast<FSaveDirtyPackages>(&FEditorFileUtils::SaveDirtyPackages);
    using FLoadMap = bool (*)(const FString&, bool, const bool);
    (void)static_cast<FLoadMap>(&FEditorFileUtils::LoadMap);
    using FSaveMap = bool (*)(UWorld*, const FString&);
    (void)static_cast<FSaveMap>(&UEditorLoadingAndSavingUtils::SaveMap);
    using FSavePackages = bool (*)(const TArray<UPackage*>&, bool);
    (void)static_cast<FSavePackages>(&UEditorLoadingAndSavingUtils::SavePackages);
    using FUnloadPackages = bool (*)(UPackageTools::FUnloadPackageParams&);
    (void)static_cast<FUnloadPackages>(&UPackageTools::UnloadPackages);
    (void)&UWorld::GetWorldPartition;
    (void)&ULevel::IsUsingExternalActors;
    (void)&ULevel::GetLoadedExternalObjectPackages;
    (void)&UPackage::IsDirty;
    (void)&UPackage::GetPersistentGuid;
    (void)&FEditorDelegates::MapChange;
    (void)&FEditorDelegates::OnMapOpened;
    (void)&FEditorDelegates::PostUndoRedo;
    (void)&UPackage::PackageDirtyStateChangedEvent;
    (void)&UGameMapsSettings::SetGlobalDefaultGameMode;
    (void)&FStructureEditorUtils::CreateUserDefinedStruct;
    (void)&FStructureEditorUtils::MoveVariable;
    (void)&FDataTableEditorUtils::AddRow;
    (void)&FDataTableEditorUtils::RenameRow;
}
}
