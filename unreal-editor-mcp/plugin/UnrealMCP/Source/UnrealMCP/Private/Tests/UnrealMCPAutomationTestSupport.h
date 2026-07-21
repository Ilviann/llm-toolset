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
#include "Engine/Texture2D.h"
#include "Engine/SCS_Node.h"
#include "Engine/Selection.h"
#include "Engine/SimpleConstructionScript.h"
#include "Editor/Transactor.h"
#include "GameFramework/Actor.h"
#include "GameFramework/InputSettings.h"
#include "HttpPath.h"
#include "HttpRequestHandler.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
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
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPBlueprintActionCatalog.h"
#include "UnrealMCPBlueprintGraphEditor.h"
#include "UnrealMCPBlueprintMutator.h"
#include "UnrealMCPCompatibility.h"
#include "UnrealMCPProtocol.h"
#include "UnrealMCPOperationLedger.h"
#include "UnrealMCPPropertyCodec.h"
#include "UnrealMCPK2TypeCodec.h"
#include "UnrealMCPTokenStore.h"
#include "UnrealMCPVersion.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"

namespace UnrealMCP::Tests
{
inline const FHttpPath Phase1RoutePath(TEXT("/unreal-mcp/v1/command"));

inline TSharedPtr<FHttpServerRequest> MakeRequest(const FString& Authorization, int32 BodyBytes = INDEX_NONE)
{
    const TSharedPtr<FHttpServerRequest> Request = MakeShared<FHttpServerRequest>();
    Request->RelativePath = Phase1RoutePath;
    Request->Verb = EHttpServerRequestVerbs::VERB_POST;
    Request->Headers.FindOrAdd(TEXT("authorization")).Add(Authorization);
    if (BodyBytes == INDEX_NONE)
    {
        const FString Json = TEXT("{\"command\":\"capabilities\",\"arguments\":{}}");
        FTCHARToUTF8 Encoded(*Json);
        Request->Body.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
    }
    else
    {
        Request->Body.SetNumZeroed(BodyBytes);
    }
    return Request;
}

inline bool LoadLiveToken(FString& OutToken)
{
    if (!FFileHelper::LoadFileToString(OutToken, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMCP"), TEXT("bridge.token"))))
    {
        return false;
    }
    OutToken.TrimStartAndEndInline();
    return FUnrealMCPTokenStore::IsValidToken(OutToken);
}

inline TSharedRef<FJsonObject> InspectArguments(const FString& AssetPath, int32 PageSize = 100)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("mode"), TEXT("inspect"));
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    Arguments->SetNumberField(TEXT("page_size"), PageSize);
    return Arguments;
}

inline TSharedRef<FJsonObject> AllSectionArguments(const FString& AssetPath, int32 PageSize = 100)
{
    TSharedRef<FJsonObject> Arguments = InspectArguments(AssetPath, PageSize);
    TArray<TSharedPtr<FJsonValue>> Sections;
    for (const TCHAR* Name : {TEXT("summary"), TEXT("parent_class"), TEXT("compile_state"), TEXT("components"),
        TEXT("variables"), TEXT("functions"), TEXT("macros"), TEXT("custom_events"), TEXT("parameters"), TEXT("local_variables"),
        TEXT("graphs"), TEXT("nodes"), TEXT("pins"), TEXT("connections")})
    {
        Sections.Add(MakeShared<FJsonValueString>(Name));
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

inline bool ResultHasSection(const TSharedPtr<FJsonObject>& Result, const FString& Section)
{
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr) return false;
    for (const TSharedPtr<FJsonValue>& Item : *Records)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FString Value;
        if (Item.IsValid() && Item->TryGetObject(Object) && Object != nullptr && Object->IsValid()
            && (*Object)->TryGetStringField(TEXT("section"), Value) && Value == Section)
        {
            return true;
        }
    }
    return false;
}

inline bool ResultHasUnsupportedType(const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr) return false;
    for (const TSharedPtr<FJsonValue>& Item : *Records)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        const TSharedPtr<FJsonObject>* Type = nullptr;
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

inline TSharedRef<FJsonObject> CreateArguments(const FString& ParentClass, const FString& PackagePath)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("parent_class"), ParentClass);
    Arguments->SetStringField(TEXT("package_path"), PackagePath);
    return Arguments;
}

inline TSharedRef<FJsonObject> AssetArguments(const FString& AssetPath)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    return Arguments;
}

inline FString InspectSnapshot(FUnrealMCPBlueprintInspector& Inspector, const FString& AssetPath)
{
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    return Inspector.Execute(InspectArguments(AssetPath), Result, Error) && Result.IsValid()
        ? Result->GetStringField(TEXT("snapshot_id")) : FString();
}

inline TSharedRef<FJsonObject> ComponentEditArguments(const FString& AssetPath, const FString& Snapshot, const FString& Operation)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    Arguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
    Arguments->SetStringField(TEXT("operation"), Operation);
    return Arguments;
}

inline FString ComponentIdByName(FUnrealMCPBlueprintInspector& Inspector, const FString& AssetPath, const FString& Name)
{
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    const TSharedRef<FJsonObject> Arguments = InspectArguments(AssetPath);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("components"))});
    if (!Inspector.Execute(Arguments, Result, Error) || !Result.IsValid()) return FString();
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr) return FString();
    for (const TSharedPtr<FJsonValue>& Item : *Records)
    {
        const TSharedPtr<FJsonObject>* RecordObject = nullptr;
        FString RecordName;
        FString Id;
        if (Item.IsValid() && Item->TryGetObject(RecordObject) && RecordObject != nullptr && RecordObject->IsValid()
            && (*RecordObject)->TryGetStringField(TEXT("name"), RecordName) && RecordName == Name
            && (*RecordObject)->TryGetStringField(TEXT("id"), Id)) return Id;
    }
    return FString();
}

inline TSharedRef<FJsonObject> MemberEditArguments(const FString& AssetPath, const FString& Snapshot, const FString& Operation)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    Arguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
    Arguments->SetStringField(TEXT("operation"), Operation);
    return Arguments;
}

inline TSharedRef<FJsonObject> ScopedMemberEditArguments(
    const FString& AssetPath,
    const FString& Snapshot,
    const FString& Target,
    const FString& Operation)
{
    TSharedRef<FJsonObject> Arguments = MemberEditArguments(AssetPath, Snapshot, Operation);
    Arguments->SetStringField(TEXT("target"), Target);
    return Arguments;
}

inline TSharedRef<FJsonObject> K2Type(const FString& Category, const FString& Container = TEXT("none"))
{
    const TSharedRef<FJsonObject> Type = MakeShared<FJsonObject>();
    Type->SetStringField(TEXT("category"), Category);
    Type->SetStringField(TEXT("container"), Container);
    return Type;
}

inline TSharedRef<FJsonObject> LiteralDefault(const TSharedPtr<FJsonValue>& Value)
{
    const TSharedRef<FJsonObject> Default = MakeShared<FJsonObject>();
    Default->SetStringField(TEXT("kind"), TEXT("literal"));
    Default->SetField(TEXT("value"), Value);
    return Default;
}

inline TSharedRef<FJsonObject> FunctionParameter(
    const FString& Name,
    const FString& Direction,
    const TSharedRef<FJsonObject>& Type,
    const TSharedPtr<FJsonObject>& Default = nullptr)
{
    const TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
    Parameter->SetStringField(TEXT("name"), Name);
    Parameter->SetStringField(TEXT("direction"), Direction);
    Parameter->SetObjectField(TEXT("type"), Type);
    if (Default.IsValid()) Parameter->SetObjectField(TEXT("default"), Default);
    return Parameter;
}

inline TSharedRef<FJsonObject> FunctionSignature(
    const FString& Access,
    bool bPure,
    bool bConst,
    const TArray<TSharedPtr<FJsonValue>>& Parameters)
{
    const TSharedRef<FJsonObject> Signature = MakeShared<FJsonObject>();
    Signature->SetStringField(TEXT("access"), Access);
    Signature->SetBoolField(TEXT("pure"), bPure);
    Signature->SetBoolField(TEXT("const"), bConst);
    Signature->SetArrayField(TEXT("parameters"), Parameters);
    return Signature;
}

inline TSharedRef<FJsonObject> MacroSignature(bool bPure, const TArray<TSharedPtr<FJsonValue>>& Parameters)
{
    const TSharedRef<FJsonObject> Signature = MakeShared<FJsonObject>();
    Signature->SetBoolField(TEXT("pure"), bPure);
    Signature->SetArrayField(TEXT("parameters"), Parameters);
    return Signature;
}

inline TSharedRef<FJsonObject> CustomEventParameter(
    const FString& Name,
    const TSharedRef<FJsonObject>& Type,
    const TSharedPtr<FJsonObject>& Default = nullptr)
{
    const TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
    Parameter->SetStringField(TEXT("name"), Name);
    Parameter->SetObjectField(TEXT("type"), Type);
    if (Default.IsValid()) Parameter->SetObjectField(TEXT("default"), Default);
    return Parameter;
}

inline TSharedRef<FJsonObject> CustomEventSignature(const TArray<TSharedPtr<FJsonValue>>& Parameters)
{
    const TSharedRef<FJsonObject> Signature = MakeShared<FJsonObject>();
    Signature->SetArrayField(TEXT("parameters"), Parameters);
    return Signature;
}

inline FString ScopedIdByName(
    FUnrealMCPBlueprintInspector& Inspector,
    const FString& AssetPath,
    const FString& Section,
    const FString& Name)
{
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    const TSharedRef<FJsonObject> Arguments = InspectArguments(AssetPath);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(Section)});
    if (!Inspector.Execute(Arguments, Result, Error) || !Result.IsValid()) return FString();
    for (const TSharedPtr<FJsonValue>& Item : Result->GetArrayField(TEXT("records")))
    {
        const TSharedPtr<FJsonObject>* RecordObject = nullptr;
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
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    const TSharedRef<FJsonObject> Arguments = InspectArguments(AssetPath);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("variables"))});
    if (!Inspector.Execute(Arguments, Result, Error) || !Result.IsValid()) return FString();
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr) return FString();
    for (const TSharedPtr<FJsonValue>& Item : *Records)
    {
        const TSharedPtr<FJsonObject>* RecordObject = nullptr;
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
