#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Engine/SCS_Node.h"
#include "Engine/Selection.h"
#include "Engine/SimpleConstructionScript.h"
#include "Editor/Transactor.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameMapsSettings.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/InputSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_MacroInstance.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPVersion.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"

namespace UnrealMCP::Tests
{
inline TSharedRef<FUnrealMCPRecord> InspectArguments(const FString& AssetPath, int32 PageSize = 100)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(TEXT("mode"), TEXT("inspect"));
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    Arguments->SetNumberField(TEXT("page_size"), PageSize);
    return Arguments;
}

inline TSharedRef<FUnrealMCPRecord> AllSectionArguments(const FString& AssetPath, int32 PageSize = 100)
{
    TSharedRef<FUnrealMCPRecord> Arguments = InspectArguments(AssetPath, PageSize);
    TArray<TSharedPtr<FUnrealMCPValue>> Sections;
    for (const TCHAR* Name : {TEXT("summary"), TEXT("parent_class"), TEXT("compile_state"), TEXT("components"),
        TEXT("variables"), TEXT("functions"), TEXT("macros"), TEXT("custom_events"), TEXT("parameters"), TEXT("local_variables"),
        TEXT("graphs"), TEXT("nodes"), TEXT("pins"), TEXT("connections")})
    {
        Sections.Add(MakeShared<FUnrealMCPValueString>(Name));
    }
    Arguments->SetArrayField(TEXT("sections"), Sections);
    return Arguments;
}

inline UBlueprint* CreateBlueprintFixture(const FString& PackageName, UClass* ParentClass, bool bAddStructure)
{
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    const FString ObjectPath = PackageName + TEXT(".") + AssetName;
    if (UBlueprint* Existing = FindObject<UBlueprint>(nullptr, *ObjectPath))
    {
        return Existing;
    }
    if (FPackageName::DoesPackageExist(PackageName))
    {
        if (UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, *ObjectPath))
        {
            return Existing;
        }
    }
    UPackage* Package = CreatePackage(*PackageName);
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        ParentClass, Package, FName(*AssetName), BPTYPE_Normal, FName(TEXT("UnrealMCP.Phase2")));
    if (Blueprint == nullptr)
    {
        return nullptr;
    }
    FAssetRegistryModule::AssetCreated(Blueprint);
    if (bAddStructure)
    {
        FEdGraphPinType IntegerType;
        IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
        FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Health"), IntegerType, TEXT("100"));
        FEdGraphPinType UnsupportedType;
        UnsupportedType.PinCategory = TEXT("unreal_mcp_unsupported");
        FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("UnsupportedValue"), UnsupportedType);
        if (Blueprint->SimpleConstructionScript != nullptr)
        {
            USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(USceneComponent::StaticClass(), TEXT("SceneRoot"));
            Blueprint->SimpleConstructionScript->AddNode(Node);
            if (USceneComponent* Template = Cast<USceneComponent>(Node->ComponentTemplate))
            {
                Template->SetRelativeLocation(FVector(10.0, 20.0, 30.0));
            }
        }
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Blueprint;
}

inline bool SaveBlueprintFixture(UBlueprint* Blueprint)
{
    if (Blueprint == nullptr) return false;
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    SaveArgs.bSlowTask = false;
    return UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, SaveArgs);
}

inline bool ResultHasSection(const TSharedPtr<FUnrealMCPRecord>& Result, const FString& Section)
{
    const TArray<TSharedPtr<FUnrealMCPValue>>* Records = nullptr;
    if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr) return false;
    for (const TSharedPtr<FUnrealMCPValue>& Item : *Records)
    {
        const TSharedPtr<FUnrealMCPRecord>* Object = nullptr;
        FString Value;
        if (Item.IsValid() && Item->TryGetObject(Object) && Object != nullptr && Object->IsValid()
            && (*Object)->TryGetStringField(TEXT("section"), Value) && Value == Section)
        {
            return true;
        }
    }
    return false;
}

inline bool ResultHasUnsupportedType(const TSharedPtr<FUnrealMCPRecord>& Result)
{
    const TArray<TSharedPtr<FUnrealMCPValue>>* Records = nullptr;
    if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr) return false;
    for (const TSharedPtr<FUnrealMCPValue>& Item : *Records)
    {
        const TSharedPtr<FUnrealMCPRecord>* Object = nullptr;
        const TSharedPtr<FUnrealMCPRecord>* Type = nullptr;
        bool bSupported = true;
        if (Item.IsValid() && Item->TryGetObject(Object) && Object != nullptr && Object->IsValid()
            && (*Object)->TryGetObjectField(TEXT("type"), Type) && Type != nullptr && Type->IsValid()
            && (*Type)->TryGetBoolField(TEXT("supported"), bSupported) && !bSupported)
        {
            return true;
        }
    }
    return false;
}

inline TSharedRef<FUnrealMCPRecord> CreateArguments(const FString& ParentClass, const FString& PackagePath)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(TEXT("parent_class"), ParentClass);
    Arguments->SetStringField(TEXT("package_path"), PackagePath);
    return Arguments;
}

inline TSharedRef<FUnrealMCPRecord> AssetArguments(const FString& AssetPath)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    return Arguments;
}

inline FString InspectSnapshot(FUnrealMCPBlueprintInspector& Inspector, const FString& AssetPath)
{
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    return Inspector.Execute(InspectArguments(AssetPath), Result, Error) && Result.IsValid()
        ? Result->GetStringField(TEXT("snapshot_id")) : FString();
}

inline TSharedRef<FUnrealMCPRecord> ComponentEditArguments(const FString& AssetPath, const FString& Snapshot, const FString& Operation)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    Arguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
    Arguments->SetStringField(TEXT("operation"), Operation);
    return Arguments;
}

inline FString ComponentIdByName(FUnrealMCPBlueprintInspector& Inspector, const FString& AssetPath, const FString& Name)
{
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    const TSharedRef<FUnrealMCPRecord> Arguments = InspectArguments(AssetPath);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FUnrealMCPValueString>(TEXT("components"))});
    if (!Inspector.Execute(Arguments, Result, Error) || !Result.IsValid()) return FString();
    const TArray<TSharedPtr<FUnrealMCPValue>>* Records = nullptr;
    if (!Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr) return FString();
    for (const TSharedPtr<FUnrealMCPValue>& Item : *Records)
    {
        const TSharedPtr<FUnrealMCPRecord>* RecordObject = nullptr;
        FString RecordName;
        FString Id;
        if (Item.IsValid() && Item->TryGetObject(RecordObject) && RecordObject != nullptr && RecordObject->IsValid()
            && (*RecordObject)->TryGetStringField(TEXT("name"), RecordName) && RecordName == Name
            && (*RecordObject)->TryGetStringField(TEXT("id"), Id)) return Id;
    }
    return FString();
}

inline TSharedRef<FUnrealMCPRecord> MemberEditArguments(const FString& AssetPath, const FString& Snapshot, const FString& Operation)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    Arguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
    Arguments->SetStringField(TEXT("operation"), Operation);
    return Arguments;
}

inline TSharedRef<FUnrealMCPRecord> ScopedMemberEditArguments(
    const FString& AssetPath,
    const FString& Snapshot,
    const FString& Target,
    const FString& Operation)
{
    TSharedRef<FUnrealMCPRecord> Arguments = MemberEditArguments(AssetPath, Snapshot, Operation);
    Arguments->SetStringField(TEXT("target"), Target);
    return Arguments;
}

inline TSharedRef<FUnrealMCPRecord> K2Type(const FString& Category, const FString& Container = TEXT("none"))
{
    const TSharedRef<FUnrealMCPRecord> Type = MakeShared<FUnrealMCPRecord>();
    Type->SetStringField(TEXT("category"), Category);
    Type->SetStringField(TEXT("container"), Container);
    return Type;
}

inline TSharedRef<FUnrealMCPRecord> LiteralDefault(const TSharedPtr<FUnrealMCPValue>& Value)
{
    const TSharedRef<FUnrealMCPRecord> Default = MakeShared<FUnrealMCPRecord>();
    Default->SetStringField(TEXT("kind"), TEXT("literal"));
    Default->SetField(TEXT("value"), Value);
    return Default;
}

inline TSharedRef<FUnrealMCPRecord> FunctionParameter(
    const FString& Name,
    const FString& Direction,
    const TSharedRef<FUnrealMCPRecord>& Type,
    const TSharedPtr<FUnrealMCPRecord>& Default = nullptr)
{
    const TSharedRef<FUnrealMCPRecord> Parameter = MakeShared<FUnrealMCPRecord>();
    Parameter->SetStringField(TEXT("name"), Name);
    Parameter->SetStringField(TEXT("direction"), Direction);
    Parameter->SetObjectField(TEXT("type"), Type);
    if (Default.IsValid()) Parameter->SetObjectField(TEXT("default"), Default);
    return Parameter;
}

inline TSharedRef<FUnrealMCPRecord> FunctionSignature(
    const FString& Access,
    bool bPure,
    bool bConst,
    const TArray<TSharedPtr<FUnrealMCPValue>>& Parameters)
{
    const TSharedRef<FUnrealMCPRecord> Signature = MakeShared<FUnrealMCPRecord>();
    Signature->SetStringField(TEXT("access"), Access);
    Signature->SetBoolField(TEXT("pure"), bPure);
    Signature->SetBoolField(TEXT("const"), bConst);
    Signature->SetArrayField(TEXT("parameters"), Parameters);
    return Signature;
}

inline TSharedRef<FUnrealMCPRecord> MacroSignature(bool bPure, const TArray<TSharedPtr<FUnrealMCPValue>>& Parameters)
{
    const TSharedRef<FUnrealMCPRecord> Signature = MakeShared<FUnrealMCPRecord>();
    Signature->SetBoolField(TEXT("pure"), bPure);
    Signature->SetArrayField(TEXT("parameters"), Parameters);
    return Signature;
}

inline TSharedRef<FUnrealMCPRecord> CustomEventParameter(
    const FString& Name,
    const TSharedRef<FUnrealMCPRecord>& Type,
    const TSharedPtr<FUnrealMCPRecord>& Default = nullptr)
{
    const TSharedRef<FUnrealMCPRecord> Parameter = MakeShared<FUnrealMCPRecord>();
    Parameter->SetStringField(TEXT("name"), Name);
    Parameter->SetObjectField(TEXT("type"), Type);
    if (Default.IsValid()) Parameter->SetObjectField(TEXT("default"), Default);
    return Parameter;
}

inline TSharedRef<FUnrealMCPRecord> CustomEventSignature(const TArray<TSharedPtr<FUnrealMCPValue>>& Parameters)
{
    const TSharedRef<FUnrealMCPRecord> Signature = MakeShared<FUnrealMCPRecord>();
    Signature->SetArrayField(TEXT("parameters"), Parameters);
    return Signature;
}

inline FString ScopedIdByName(
    FUnrealMCPBlueprintInspector& Inspector,
    const FString& AssetPath,
    const FString& Section,
    const FString& Name)
{
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    const TSharedRef<FUnrealMCPRecord> Arguments = InspectArguments(AssetPath);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FUnrealMCPValueString>(Section)});
    if (!Inspector.Execute(Arguments, Result, Error) || !Result.IsValid()) return FString();
    for (const TSharedPtr<FUnrealMCPValue>& Item : Result->GetArrayField(TEXT("records")))
    {
        const TSharedPtr<FUnrealMCPRecord>* RecordObject = nullptr;
        FString RecordName;
        FString Id;
        if (Item.IsValid() && Item->TryGetObject(RecordObject) && RecordObject != nullptr && RecordObject->IsValid()
            && (*RecordObject)->TryGetStringField(TEXT("name"), RecordName) && RecordName == Name
            && (*RecordObject)->TryGetStringField(TEXT("id"), Id)) return Id;
    }
    return FString();
}

inline FString MemberIdByName(FUnrealMCPBlueprintInspector& Inspector, const FString& AssetPath, const FString& Name)
{
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    const TSharedRef<FUnrealMCPRecord> Arguments = InspectArguments(AssetPath);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FUnrealMCPValueString>(TEXT("variables"))});
    if (!Inspector.Execute(Arguments, Result, Error) || !Result.IsValid()) return FString();
    const TArray<TSharedPtr<FUnrealMCPValue>>* Records = nullptr;
    if (!Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr) return FString();
    for (const TSharedPtr<FUnrealMCPValue>& Item : *Records)
    {
        const TSharedPtr<FUnrealMCPRecord>* RecordObject = nullptr;
        FString RecordName;
        FString Id;
        if (Item.IsValid() && Item->TryGetObject(RecordObject) && RecordObject != nullptr && RecordObject->IsValid()
            && (*RecordObject)->TryGetStringField(TEXT("name"), RecordName) && RecordName == Name
            && (*RecordObject)->TryGetStringField(TEXT("id"), Id)) return Id;
    }
    return FString();
}
}

#endif
