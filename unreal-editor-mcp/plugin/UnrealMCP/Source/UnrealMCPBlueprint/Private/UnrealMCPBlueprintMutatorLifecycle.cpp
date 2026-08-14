#include "UnrealMCPBlueprintCallableMutationSupport.h"
#include "UnrealMCPAssetAuthoringKernel.h"
#include "Blueprint/UserWidget.h"
#include "WidgetBlueprintOperationUtils.h"


FUnrealMCPBlueprintMutator::FUnrealMCPBlueprintMutator(
    FUnrealMCPBlueprintInspector& InInspector,
    FCompile InCompile,
    FSave InSave)
    : Inspector(InInspector)
    , CompileBlueprint(InCompile ? MoveTemp(InCompile) : FCompile([](UBlueprint* Blueprint, FCompilerResultsLog& Log)
      {
          FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
      }))
    , SaveBlueprint(InSave ? MoveTemp(InSave) : FSave([](UBlueprint* Blueprint)
      {
          const FString Filename = FPackageName::LongPackageNameToFilename(
              Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
          FSavePackageArgs SaveArgs;
          SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
          SaveArgs.SaveFlags = SAVE_NoError;
          SaveArgs.bSlowTask = false;
          return UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, SaveArgs);
      }))
{
}

bool FUnrealMCPBlueprintMutator::Execute(
    const FString& Command,
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    check(IsInGameThread());
    if (!Arguments.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("arguments must be an object")};
        return false;
    }
    if (Command == TEXT("blueprint_create")) return Create(Arguments, OutResult, OutError);
    if (Command == TEXT("blueprint_compile")) return Compile(Arguments, OutResult, OutError);
    if (Command == TEXT("blueprint_save")) return Save(Arguments, OutResult, OutError);
    if (Command == TEXT("blueprint_component_edit")) return ComponentEdit(Arguments, OutResult, OutError);
    if (Command == TEXT("blueprint_default_edit")) return DefaultEdit(Arguments, OutResult, OutError);
    if (Command == TEXT("blueprint_member_edit")) return MemberEdit(Arguments, OutResult, OutError);
    OutError = {TEXT("invalid_argument"), TEXT("Unknown Blueprint mutation command")};
    return false;
}

bool FUnrealMCPBlueprintMutator::Create(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    if (!HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("parent_class"), TEXT("package_path")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("blueprint_create accepts only parent_class and package_path")};
        return false;
    }
    FString ParentPath;
    FString RawPackagePath;
    FString PackageName;
    if (!Arguments->TryGetStringField(TEXT("parent_class"), ParentPath)
        || !Arguments->TryGetStringField(TEXT("package_path"), RawPackagePath)
        || !NormalizePackagePath(RawPackagePath, PackageName))
    {
        OutError = {TEXT("invalid_argument"), TEXT("package_path must be one valid long package name without an object suffix")};
        return false;
    }
    UClass* ParentClass = nullptr;
    if (!ResolveParent(ParentPath, ParentClass, OutError))
    {
        return false;
    }
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    FCompilerResultsLog Log;
    Log.bSilentMode = true;
    FString OperationId;
    Arguments->TryGetStringField(TEXT("operation_id"), OperationId);
    const FString ObjectPath = FUnrealMCPAssetAuthoringKernel::ObjectPathForPackage(PackageName);
    FUnrealMCPAssetCreationRequest Request{OperationId, PackageName, ObjectPath};
    FUnrealMCPAssetCreationHooks Hooks;
    Hooks.Create = [ParentClass, AssetName](UPackage* Package, UObject*& OutAsset, FUnrealMCPError&)
    {
        OutAsset = ParentClass->IsChildOf(UUserWidget::StaticClass())
            ? FWidgetBlueprintOperationUtils::CreateWidgetBlueprint(
                Package, FName(*AssetName), BPTYPE_Normal, ParentClass, nullptr,
                FName(TEXT("UnrealMCP")), false)
            : FKismetEditorUtilities::CreateBlueprint(
                ParentClass, Package, FName(*AssetName), BPTYPE_Normal,
                FName(TEXT("UnrealMCP")));
        return OutAsset != nullptr;
    };
    Hooks.Finalize = [this, &Log](UObject* Asset, FUnrealMCPError& Error)
    {
        UBlueprint* Blueprint = CastChecked<UBlueprint>(Asset);
        CompileBlueprint(Blueprint, Log);
        if (Log.NumErrors == 0 && Blueprint->Status != BS_Error)
        {
            return true;
        }
        const FString First = Log.Messages.IsEmpty()
            ? FString()
            : Log.Messages[0]->ToText().ToString().Left(UnrealMCP::MaxDiagnosticChars);
        Error = {TEXT("compile_failed"), TEXT("The new Blueprint did not compile")};
        Error.Details->SetNumberField(TEXT("diagnostic_count"), Log.Messages.Num());
        if (!First.IsEmpty())
        {
            Error.Details->SetStringField(TEXT("first_diagnostic"), First);
        }
        return false;
    };
    Hooks.Persist = [this](UObject* Asset, FUnrealMCPError& Error)
    {
        if (SaveBlueprint(CastChecked<UBlueprint>(Asset)))
        {
            return true;
        }
        Error = {TEXT("save_failed"), TEXT("The new Blueprint package could not be saved")};
        return false;
    };
    Hooks.ReadBack = [this](UObject* Asset, FString& Snapshot, FUnrealMCPError& Error)
    {
        return ReadSnapshot(Inspector, Asset->GetPathName(), Snapshot, Error);
    };
    FUnrealMCPAssetCreationResult Creation;
    if (!FUnrealMCPAssetAuthoringKernel::ExecuteCreation(
            Request, Hooks, Creation, OutError))
    {
        return false;
    }
    UBlueprint* Blueprint = CastChecked<UBlueprint>(Creation.Asset);
    OutResult = BuildResult(
        Blueprint, Creation.ObjectPath, Creation.SnapshotId, &Log, true, true);
    return true;
}

bool FUnrealMCPBlueprintMutator::Compile(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    if (Arguments->HasField(TEXT("operation_id")) && !Arguments->HasField(TEXT("expected_snapshot")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("expected_snapshot is required for a reconciled compile")};
        return false;
    }
    UBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    FString PackageName;
    if (!ResolveMutableBlueprint(*Arguments, Blueprint, ObjectPath, PackageName, OutError,
        UnrealMCP::BlueprintFamilyPolicy::EOperation::Compile))
    {
        return false;
    }
    if (!ValidateExpectedSnapshot(Inspector, *Arguments, ObjectPath, OutError)) return false;
    FCompilerResultsLog Log;
    Log.bSilentMode = true;
    CompileBlueprint(Blueprint, Log);
    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        return false;
    }
    const bool bCompiled = Log.NumErrors == 0 && Blueprint->Status != BS_Error;
    OutResult = BuildResult(Blueprint, ObjectPath, Snapshot, &Log, bCompiled, false);
    return true;
}

bool FUnrealMCPBlueprintMutator::Save(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    if (Arguments->HasField(TEXT("operation_id")) && !Arguments->HasField(TEXT("expected_snapshot")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("expected_snapshot is required for a reconciled save")};
        return false;
    }
    UBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    FString PackageName;
    if (!ResolveMutableBlueprint(*Arguments, Blueprint, ObjectPath, PackageName, OutError,
        UnrealMCP::BlueprintFamilyPolicy::EOperation::Save))
    {
        return false;
    }
    if (!ValidateExpectedSnapshot(Inspector, *Arguments, ObjectPath, OutError)) return false;
    FString Filename;
    if (!ValidateWritableTarget(PackageName, Filename, OutError))
    {
        return false;
    }
    if (!SaveBlueprint(Blueprint))
    {
        OutError = {TEXT("save_failed"), TEXT("The Blueprint package could not be saved")};
        return false;
    }
    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        return false;
    }
    OutResult = BuildResult(Blueprint, ObjectPath, Snapshot, nullptr, Blueprint->Status != BS_Error, true);
    return true;
}
