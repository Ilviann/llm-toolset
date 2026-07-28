#include "UnrealMCPGameDataService.h"
#include "UnrealMCPGameDataInspectionBuilder.h"
#include "UnrealMCPGameDataRequestValidation.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "DataTableEditorUtils.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EdGraphSchema_K2.h"
#include "Engine/DataTable.h"
#include "Factories/DataTableFactory.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UnrealMCPGameDataValueCodec.h"
#include "UnrealMCPK2TypeCodec.h"
#include "UnrealMCPVersion.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectGlobals.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"

namespace
{
namespace RequestValidation = UnrealMCP::GameDataRequestValidation;
namespace InspectionBuilder = UnrealMCP::GameDataInspectionBuilder;

struct FStagedRow
{
    FString Name;
    bool bPreserve = false;
    TSharedPtr<FStructOnScope> Data;
};

bool ContainsSymlink(const FString& Root, const FString& Candidate)
{
    IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
    FString Current = Root; FPaths::NormalizeDirectoryName(Current);
    if (Platform.IsSymlink(*Current) == ESymlinkResult::Symlink) return true;
    FString Relative = Candidate; FPaths::NormalizeDirectoryName(Relative);
    if (!FPaths::MakePathRelativeTo(Relative, *(Current + TEXT("/")))) return true;
    TArray<FString> Segments; Relative.ParseIntoArray(Segments, TEXT("/"), true);
    for (const FString& Segment : Segments)
    {
        Current /= Segment;
        if ((Platform.FileExists(*Current) || Platform.DirectoryExists(*Current))
            && Platform.IsSymlink(*Current) == ESymlinkResult::Symlink) return true;
    }
    return false;
}

bool ValidateMutationScope(const FString& PackageName, FUnrealMCPError& OutError)
{
    FString Target;
    if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, Target))
    { OutError = {TEXT("mutation_scope_denied"), TEXT("The content mount is unavailable")}; return false; }
    Target = FPaths::ConvertRelativePathToFull(FPaths::GetPath(Target)); FPaths::NormalizeDirectoryName(Target);
    if (PackageName.StartsWith(TEXT("/Game/")))
    {
        FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()); FPaths::NormalizeDirectoryName(Root);
        if ((FPaths::IsSamePath(Target, Root) || FPaths::IsUnderDirectory(Target, Root)) && !ContainsSymlink(Root, Target)) return true;
        OutError = {TEXT("mutation_scope_denied"), TEXT("Project content resolves outside its physical mount")}; return false;
    }
    const int32 Slash = PackageName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
    FString MountDirectory;
    if (Slash == INDEX_NONE || !FPackageName::TryConvertLongPackageNameToFilename(PackageName.Left(Slash + 1), MountDirectory))
    { OutError = {TEXT("mutation_scope_denied"), TEXT("Only project content and local project-plugin content are mutable")}; return false; }
    FString PluginRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir()); FPaths::NormalizeDirectoryName(PluginRoot);
    FString Mount = FPaths::ConvertRelativePathToFull(MountDirectory); FPaths::NormalizeDirectoryName(Mount);
    if (!FPaths::IsUnderDirectory(Mount, PluginRoot) || !FPaths::IsUnderDirectory(Target, PluginRoot) || ContainsSymlink(PluginRoot, Target))
    { OutError = {TEXT("mutation_scope_denied"), TEXT("Only symlink-free local project-plugin mounts are mutable")}; return false; }
    FString Candidate = Mount;
    while (FPaths::IsUnderDirectory(Candidate, PluginRoot))
    {
        TArray<FString> Descriptors; IFileManager::Get().FindFiles(Descriptors, *(Candidate / TEXT("*.uplugin")), true, false);
        if (!Descriptors.IsEmpty()) return true;
        const FString Parent = FPaths::GetPath(Candidate); if (Parent == Candidate) break; Candidate = Parent;
    }
    OutError = {TEXT("mutation_scope_denied"), TEXT("The content mount is not owned by a local project plugin")}; return false;
}

bool WritableFilename(const FString& PackageName, FString& OutFilename, FUnrealMCPError& OutError)
{
    if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, OutFilename, FPackageName::GetAssetPackageExtension()))
    { OutError = {TEXT("mutation_scope_denied"), TEXT("The package does not resolve to mounted content")}; return false; }
    FString Directory = FPaths::GetPath(OutFilename);
    while (!Directory.IsEmpty() && !IFileManager::Get().DirectoryExists(*Directory))
    { const FString Parent = FPaths::GetPath(Directory); if (Parent == Directory) break; Directory = Parent; }
    if (Directory.IsEmpty() || IFileManager::Get().IsReadOnly(*Directory)
        || (IFileManager::Get().FileExists(*OutFilename) && IFileManager::Get().IsReadOnly(*OutFilename)))
    { OutError = {TEXT("write_conflict"), TEXT("The package destination is read-only or unavailable")}; return false; }
    return true;
}

bool PackageExists(const FString& PackageName)
{
    if (FindPackage(nullptr, *PackageName) != nullptr
        || FindObject<UObject>(nullptr, *RequestValidation::ObjectPathForPackage(PackageName)) != nullptr) return true;
    FString Filename;
    if (FPackageName::DoesPackageExist(PackageName, &Filename) || IFileManager::Get().FileExists(*Filename)) return true;
    const FAssetData Existing = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
        .Get().GetAssetByObjectPath(FSoftObjectPath(RequestValidation::ObjectPathForPackage(PackageName)));
    return Existing.IsValid();
}

bool SaveAsset(UObject* Asset)
{
    if (Asset == nullptr) return false;
    const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError; Args.bSlowTask = false;
    if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, Args)) return false;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().ScanModifiedAssetFiles({Filename});
    return true;
}

void CleanupCreation(UPackage* Package, UObject* Asset, const FString& Filename, bool bPublished)
{
    if (Asset != nullptr)
    {
        if (bPublished) FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().AssetDeleted(Asset);
        Asset->ClearFlags(RF_Public | RF_Standalone); Asset->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
        Asset->MarkAsGarbage();
    }
    if (Package != nullptr) { Package->ClearFlags(RF_Public | RF_Standalone); Package->SetDirtyFlag(false); }
    if (!Filename.IsEmpty() && IFileManager::Get().FileExists(*Filename)) IFileManager::Get().Delete(*Filename, false, true, true);
}

bool ResolveStruct(const FString& Path, UScriptStruct*& Out, FUnrealMCPError& OutError)
{
    if (!Path.StartsWith(TEXT("/")) || Path.Contains(TEXT("..")) || Path.Contains(TEXT("\\")) || Path.Len() > 512)
    { OutError = {TEXT("invalid_schema"), TEXT("row_struct must be one bounded Unreal struct path")}; return false; }
    Out = LoadObject<UScriptStruct>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (Out == nullptr || !FDataTableEditorUtils::IsValidTableStruct(Out))
    { OutError = {TEXT("invalid_schema"), TEXT("row_struct must resolve to one live Data Table-compatible native or user-defined struct")}; return false; }
    FStructOnScope Defaults(Out);
    int32 Count = 0;
    for (TFieldIterator<FProperty> It(Out); It; ++It)
    {
        TSharedPtr<FJsonValue> Encoded;
        if (++Count > UnrealMCP::MaxGameDataFields || !UnrealMCP::GameDataValueCodec::EncodeType(*It)->GetBoolField(TEXT("supported"))
            || !UnrealMCP::GameDataValueCodec::Encode(*It, It->ContainerPtrToValuePtr<void>(Defaults.GetStructMemory()), 0, Encoded, OutError))
        { OutError = {TEXT("unsupported_type"), TEXT("The row schema contains too many fields or an unsupported field type")}; return false; }
    }
    return true;
}

bool ReadStructMember(const TSharedPtr<FJsonObject>& Object, FString& OutName, FEdGraphPinType& OutType,
    FString& OutDefault, FString& OutTooltip, FUnrealMCPError& OutError)
{
    const TSharedPtr<FJsonObject>* Type = nullptr; const TSharedPtr<FJsonObject>* Default = nullptr;
    if (!Object.IsValid() || !RequestValidation::HasOnlyFields(*Object, {TEXT("name"), TEXT("type"), TEXT("default"), TEXT("tooltip")})
        || !Object->TryGetStringField(TEXT("name"), OutName) || !RequestValidation::ValidName(OutName)
        || !Object->TryGetObjectField(TEXT("type"), Type) || Type == nullptr
        || (Object->HasField(TEXT("tooltip")) && (!Object->TryGetStringField(TEXT("tooltip"), OutTooltip) || OutTooltip.Len() > 512))
        || !UnrealMCP::K2TypeCodec::DecodeType(*Type, OutType, OutError))
    { if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_schema"), TEXT("A struct member has an invalid exact shape")}; return false; }
    if (OutType.bIsReference || OutType.bIsConst)
    { OutError = {TEXT("unsupported_type"), TEXT("Struct members cannot be reference or const types")}; return false; }
    if (Object->HasField(TEXT("default")))
    {
        if (!Object->TryGetObjectField(TEXT("default"), Default) || Default == nullptr
            || !UnrealMCP::K2TypeCodec::DecodeDefault(OutType, *Default, OutDefault, OutError)) return false;
    }
    return true;
}

bool ValidateExpected(const FJsonObject& Arguments, const FString& Actual, FUnrealMCPError& OutError)
{
    FString Expected;
    if (!Arguments.TryGetStringField(TEXT("expected_snapshot"), Expected) || Expected.Len() != 40 || Expected != Actual)
    { OutError = {TEXT("stale_precondition"), TEXT("The game-data structural snapshot changed")}; return false; }
    return true;
}

bool RestoreAfterFailure(UObject* Asset, FUnrealMCPError& OutError)
{
    if (GEditor != nullptr && GEditor->UndoTransaction() && (Asset == nullptr || SaveAsset(Asset))) return true;
    OutError = {TEXT("internal_error"), TEXT("The game-data mutation failed and explicit restoration was unavailable")}; return false;
}

bool StageRows(const UScriptStruct* Struct, const TArray<TSharedPtr<FJsonValue>>& Items, const UDataTable* Existing,
    TArray<FStagedRow>& Out, FUnrealMCPError& OutError)
{
    if (Items.Num() > UnrealMCP::MaxGameDataBatchRows) { OutError = {TEXT("data_limit_exceeded"), TEXT("The row batch exceeds the configured limit")}; return false; }
    TSet<FName> Names;
    for (const TSharedPtr<FJsonValue>& Item : Items)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr; FString Name; bool bPreserve = false; const TSharedPtr<FJsonObject>* Values = nullptr;
        if (!Item->TryGetObject(Object) || Object == nullptr || !(*Object).IsValid()
            || !RequestValidation::HasOnlyFields(**Object, {TEXT("row_name"), TEXT("values"), TEXT("preserve_unspecified")})
            || !(*Object)->TryGetStringField(TEXT("row_name"), Name) || !RequestValidation::ValidName(Name)
            || !(*Object)->TryGetObjectField(TEXT("values"), Values) || Values == nullptr
            || ((*Object)->HasField(TEXT("preserve_unspecified")) && !(*Object)->TryGetBoolField(TEXT("preserve_unspecified"), bPreserve))
            || Names.Contains(FName(*Name)))
        { OutError = {TEXT("invalid_row"), TEXT("A row write has invalid fields or a duplicate/case-conflicting name")}; return false; }
        Names.Add(FName(*Name));
        FStagedRow Row; Row.Name = Name; Row.bPreserve = bPreserve; Row.Data = MakeShared<FStructOnScope>(Struct);
        if (bPreserve)
        {
            const uint8* Current = Existing != nullptr ? Existing->FindRowUnchecked(FName(*Name)) : nullptr;
            if (Current == nullptr) { OutError = {TEXT("invalid_row"), TEXT("preserve_unspecified requires an existing row")}; return false; }
            Struct->CopyScriptStruct(Row.Data->GetStructMemory(), Current);
        }
        if (!UnrealMCP::GameDataValueCodec::ApplyFields(Struct, Row.Data->GetStructMemory(), *Values, OutError)) return false;
        Out.Add(MoveTemp(Row));
    }
    return true;
}
}

bool FUnrealMCPGameDataService::Edit(const TSharedPtr<FJsonObject>& Arguments, TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    FString Target, Operation, InputPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("target"), Target) || !Arguments->TryGetStringField(TEXT("operation"), Operation)
        || !Arguments->TryGetStringField(TEXT("asset_path"), InputPath)
        || (Target != TEXT("user_defined_struct") && Target != TEXT("data_table")))
    { OutError = {TEXT("invalid_argument"), TEXT("game_data_edit requires one exact target, operation, and asset_path")}; return false; }
    if (!RequestValidation::ValidateEditShape(*Arguments, Target, Operation, OutError)) return false;

    if (Operation == TEXT("create"))
    {
        FString PackageName;
        if (!RequestValidation::NormalizePackagePath(InputPath, PackageName))
        { OutError = {TEXT("invalid_argument"), TEXT("create asset_path must be one exact bounded Unreal package path")}; return false; }
        if (!ValidateMutationScope(PackageName, OutError)) return false;
        if (PackageExists(PackageName))
        { OutError = {TEXT("already_exists"), TEXT("The destination package or asset already exists")}; return false; }
        FString Filename; if (!WritableFilename(PackageName, Filename, OutError)) return false;
        const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName); UPackage* Package = CreatePackage(*PackageName); UObject* Asset = nullptr;
        if (Target == TEXT("user_defined_struct"))
        {
            const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
            if (!RequestValidation::HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("members")})
                || !Arguments->TryGetArrayField(TEXT("members"), Items) || Items == nullptr || Items->IsEmpty() || Items->Num() > UnrealMCP::MaxGameDataFields)
            { CleanupCreation(Package, nullptr, Filename, false); OutError = {TEXT("invalid_schema"), TEXT("Struct creation requires one bounded non-empty members array")}; return false; }
            struct FMember { FString Name; FEdGraphPinType Type; FString Default; FString Tooltip; };
            TArray<FMember> Members; TSet<FString> Folded;
            for (const TSharedPtr<FJsonValue>& Item : *Items)
            {
                const TSharedPtr<FJsonObject>* Object = nullptr; FMember Member;
                if (!Item->TryGetObject(Object) || Object == nullptr || !ReadStructMember(*Object, Member.Name, Member.Type, Member.Default, Member.Tooltip, OutError)
                    || Folded.Contains(Member.Name.ToLower()))
                { CleanupCreation(Package, nullptr, Filename, false); if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_schema"), TEXT("Struct member names must be unique ignoring case")}; return false; }
                Folded.Add(Member.Name.ToLower()); Members.Add(MoveTemp(Member));
            }
            UUserDefinedStruct* Struct = FStructureEditorUtils::CreateUserDefinedStruct(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
            Asset = Struct;
            if (Struct == nullptr) { CleanupCreation(Package, nullptr, Filename, false); OutError = {TEXT("internal_error"), TEXT("Unreal could not create the user-defined struct")}; return false; }
            const FGuid FirstId = FStructureEditorUtils::GetVarDesc(Struct)[0].VarGuid;
            if (!FStructureEditorUtils::RenameVariable(Struct, FirstId, Members[0].Name)
                || !FStructureEditorUtils::ChangeVariableType(Struct, FirstId, Members[0].Type))
            { CleanupCreation(Package, Asset, Filename, false); OutError = {TEXT("invalid_schema"), TEXT("Unreal rejected the first struct member")}; return false; }
            if ((!Members[0].Default.IsEmpty() && !FStructureEditorUtils::ChangeVariableDefaultValue(Struct, FirstId, Members[0].Default))
                || (!Members[0].Tooltip.IsEmpty() && !FStructureEditorUtils::ChangeVariableTooltip(Struct, FirstId, Members[0].Tooltip)))
            { CleanupCreation(Package, Asset, Filename, false); OutError = {TEXT("invalid_schema"), TEXT("Unreal rejected the first struct member default or tooltip")}; return false; }
            for (int32 Index = 1; Index < Members.Num(); ++Index)
            {
                if (!FStructureEditorUtils::AddVariable(Struct, Members[Index].Type)) { CleanupCreation(Package, Asset, Filename, false); OutError = {TEXT("invalid_schema"), TEXT("Unreal rejected a struct member type")}; return false; }
                FStructVariableDescription& Added = FStructureEditorUtils::GetVarDesc(Struct).Last();
                if (!FStructureEditorUtils::RenameVariable(Struct, Added.VarGuid, Members[Index].Name)
                    || (!Members[Index].Default.IsEmpty() && !FStructureEditorUtils::ChangeVariableDefaultValue(Struct, Added.VarGuid, Members[Index].Default))
                    || (!Members[Index].Tooltip.IsEmpty() && !FStructureEditorUtils::ChangeVariableTooltip(Struct, Added.VarGuid, Members[Index].Tooltip)))
                { CleanupCreation(Package, Asset, Filename, false); OutError = {TEXT("invalid_schema"), TEXT("Unreal rejected a struct member name, default, or tooltip")}; return false; }
            }
            FStructureEditorUtils::CompileStructure(Struct);
            if (Struct->Status != UDSS_UpToDate) { CleanupCreation(Package, Asset, Filename, false); OutError = {TEXT("compile_failed"), TEXT("The new user-defined struct did not compile")}; return false; }
        }
        else
        {
            const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr; FString RowStructPath; UScriptStruct* RowStruct = nullptr;
            if (!RequestValidation::HasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("row_struct"), TEXT("rows")})
                || !Arguments->TryGetStringField(TEXT("row_struct"), RowStructPath) || !ResolveStruct(RowStructPath, RowStruct, OutError)
                || (Arguments->HasField(TEXT("rows")) && !Arguments->TryGetArrayField(TEXT("rows"), Rows)))
            { CleanupCreation(Package, nullptr, Filename, false); return false; }
            TArray<TSharedPtr<FJsonValue>> Empty; TArray<FStagedRow> Staged;
            if (!StageRows(RowStruct, Rows != nullptr ? *Rows : Empty, nullptr, Staged, OutError)) { CleanupCreation(Package, nullptr, Filename, false); return false; }
            UDataTableFactory* Factory = NewObject<UDataTableFactory>(); Factory->Struct = RowStruct;
            UDataTable* Table = Cast<UDataTable>(Factory->FactoryCreateNew(UDataTable::StaticClass(), Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn)); Asset = Table;
            if (Table == nullptr) { CleanupCreation(Package, nullptr, Filename, false); OutError = {TEXT("internal_error"), TEXT("Unreal could not create the Data Table")}; return false; }
            for (const FStagedRow& Row : Staged)
            {
                uint8* Added = FDataTableEditorUtils::AddRow(Table, FName(*Row.Name));
                if (Added == nullptr) { CleanupCreation(Package, Asset, Filename, false); OutError = {TEXT("invalid_row"), TEXT("Unreal rejected a staged Data Table row")}; return false; }
                RowStruct->CopyScriptStruct(Added, Row.Data->GetStructMemory());
            }
            Table->HandleDataTableChanged();
        }
        if (!SaveAsset(Asset)) { CleanupCreation(Package, Asset, Filename, false); OutError = {TEXT("save_failed"), TEXT("The new game-data package could not be saved")}; return false; }
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().AssetCreated(Asset);
        const TSharedRef<FJsonObject> InspectArgs = MakeShared<FJsonObject>(); InspectArgs->SetStringField(TEXT("target"), Target); InspectArgs->SetStringField(TEXT("asset_path"), Asset->GetPathName());
        FString ActualTarget, ObjectPath, PackagePath, Snapshot; TArray<TSharedPtr<FJsonValue>> Records, Schema; TSharedPtr<FJsonObject> Metadata;
        if (!UnrealMCP::GameDataInspectionBuilder::Build(
            *InspectArgs, ActualTarget, ObjectPath, PackagePath, Records, Schema, Snapshot, Metadata, OutError))
        { CleanupCreation(Package, Asset, Filename, true); return false; }
        const TSharedRef<FJsonObject> Result = InspectionBuilder::BuildEditResult(Target, ObjectPath, Snapshot); Result->SetStringField(TEXT("operation"), TEXT("create"));
        Result->SetNumberField(TEXT("changed_count"), Records.Num()); OutResult = Result; return true;
    }

    FString ObjectPath, PackageName;
    if (!RequestValidation::NormalizeAssetPath(InputPath, ObjectPath, PackageName)) { OutError = {TEXT("invalid_argument"), TEXT("asset_path must be one exact bounded Unreal asset path")}; return false; }
    if (!ValidateMutationScope(PackageName, OutError)) return false;
    const TSharedRef<FJsonObject> InspectArgs = MakeShared<FJsonObject>(); InspectArgs->SetStringField(TEXT("target"), Target); InspectArgs->SetStringField(TEXT("asset_path"), ObjectPath);
    FString ActualTarget, ActualObject, ActualPackage, BeforeSnapshot; TArray<TSharedPtr<FJsonValue>> BeforeRecords, BeforeSchema; TSharedPtr<FJsonObject> BeforeMetadata;
    if (!UnrealMCP::GameDataInspectionBuilder::Build(
            *InspectArgs, ActualTarget, ActualObject, ActualPackage, BeforeRecords, BeforeSchema, BeforeSnapshot, BeforeMetadata, OutError)
        || !ValidateExpected(*Arguments, BeforeSnapshot, OutError)) return false;
    UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "GameDataEdit", "Unreal MCP Game Data Edit")); Asset->Modify();
    TArray<FString> ChangedNames;
    if (Target == TEXT("user_defined_struct"))
    {
        UUserDefinedStruct* Struct = CastChecked<UUserDefinedStruct>(Asset); FGuid MemberId; FStructVariableDescription* Member = nullptr; FString ExistingMemberName;
        if (Operation != TEXT("add_member"))
        {
            if (!RequestValidation::ParseGuidField(*Arguments, TEXT("member_id"), MemberId, OutError)) { Transaction.Cancel(); return false; }
            Member = FStructureEditorUtils::GetVarDescByGuid(Struct, MemberId);
            if (Member == nullptr) { Transaction.Cancel(); OutError = {TEXT("invalid_schema"), TEXT("The struct member identity is missing or stale")}; return false; }
            ExistingMemberName = Member->FriendlyName;
        }
        bool bChanged = false;
        if (Operation == TEXT("rename_member"))
        {
            FString Name; bChanged = Arguments->TryGetStringField(TEXT("new_name"), Name) && RequestValidation::ValidName(Name) && FStructureEditorUtils::RenameVariable(Struct, MemberId, Name); ChangedNames.Add(Name);
        }
        else if (Operation == TEXT("update_member"))
        {
            FString Field; Arguments->TryGetStringField(TEXT("field"), Field);
            if (Field == TEXT("default"))
            {
                const TSharedPtr<FJsonObject>* Default = nullptr; FString Text;
                bChanged = Arguments->TryGetObjectField(TEXT("default"), Default) && Default != nullptr
                    && UnrealMCP::K2TypeCodec::DecodeDefault(Member->ToPinType(), *Default, Text, OutError)
                    && FStructureEditorUtils::ChangeVariableDefaultValue(Struct, MemberId, Text);
            }
            else if (Field == TEXT("type"))
            {
                TArray<FString> Dependencies; bool bTruncated = false; InspectionBuilder::GatherDependencies(PackageName, Dependencies, bTruncated);
                if (!Dependencies.IsEmpty() || bTruncated) { Transaction.Cancel(); OutError = {TEXT("referenced_schema"), TEXT("Struct type changes reject while dependent assets exist")}; OutError.Details->SetNumberField(TEXT("dependency_count"), Dependencies.Num()); return false; }
                const TSharedPtr<FJsonObject>* Type = nullptr; FEdGraphPinType PinType;
                bChanged = Arguments->TryGetObjectField(TEXT("type"), Type) && Type != nullptr
                    && UnrealMCP::K2TypeCodec::DecodeType(*Type, PinType, OutError) && !PinType.bIsReference && !PinType.bIsConst
                    && FStructureEditorUtils::ChangeVariableType(Struct, MemberId, PinType);
            }
            ChangedNames.Add(ExistingMemberName);
        }
        else if (Operation == TEXT("reorder_member"))
        {
            FGuid Relative; FString Position;
            bChanged = RequestValidation::ParseGuidField(*Arguments, TEXT("relative_to_member_id"), Relative, OutError)
                && Arguments->TryGetStringField(TEXT("position"), Position) && MemberId != Relative
                && FStructureEditorUtils::MoveVariable(Struct, MemberId, Relative,
                    Position == TEXT("above") ? FStructureEditorUtils::PositionAbove : FStructureEditorUtils::PositionBelow);
            ChangedNames.Add(ExistingMemberName);
        }
        else if (Operation == TEXT("remove_member"))
        {
            TArray<FString> Dependencies; bool bTruncated = false; InspectionBuilder::GatherDependencies(PackageName, Dependencies, bTruncated);
            if (!Dependencies.IsEmpty() || bTruncated) { Transaction.Cancel(); OutError = {TEXT("referenced_schema"), TEXT("Struct member removal rejects while dependent assets exist")}; OutError.Details->SetNumberField(TEXT("dependency_count"), Dependencies.Num()); return false; }
            ChangedNames.Add(ExistingMemberName); bChanged = FStructureEditorUtils::RemoveVariable(Struct, MemberId);
        }
        else if (Operation == TEXT("add_member"))
        {
            const TSharedPtr<FJsonObject>* MemberObject = nullptr; FString Name, Default, Tooltip; FEdGraphPinType Type;
            if (!Arguments->TryGetObjectField(TEXT("member"), MemberObject) || MemberObject == nullptr || !ReadStructMember(*MemberObject, Name, Type, Default, Tooltip, OutError)) { Transaction.Cancel(); return false; }
            bChanged = FStructureEditorUtils::AddVariable(Struct, Type);
            if (bChanged)
            {
                const FGuid Added = FStructureEditorUtils::GetVarDesc(Struct).Last().VarGuid;
                bChanged = FStructureEditorUtils::RenameVariable(Struct, Added, Name)
                    && (Default.IsEmpty() || FStructureEditorUtils::ChangeVariableDefaultValue(Struct, Added, Default))
                    && (Tooltip.IsEmpty() || FStructureEditorUtils::ChangeVariableTooltip(Struct, Added, Tooltip));
            }
            ChangedNames.Add(Name);
        }
        if (!bChanged || Struct->Status != UDSS_UpToDate)
        { Transaction.Cancel(); OutError = OutError.Code.IsEmpty() ? FUnrealMCPError{TEXT("no_change"), TEXT("Unreal rejected the struct edit or it made no change")} : OutError; return false; }
    }
    else
    {
        UDataTable* Table = CastChecked<UDataTable>(Asset); UScriptStruct* Struct = const_cast<UScriptStruct*>(Table->GetRowStruct());
        if (Operation == TEXT("rename_row"))
        {
            FString Old, New;
            if (!Arguments->TryGetStringField(TEXT("row_name"), Old) || !Arguments->TryGetStringField(TEXT("new_row_name"), New) || !RequestValidation::ValidName(Old) || !RequestValidation::ValidName(New)
                || Table->FindRowUnchecked(FName(*Old)) == nullptr || Table->FindRowUnchecked(FName(*New)) != nullptr || !FDataTableEditorUtils::RenameRow(Table, FName(*Old), FName(*New)))
            { Transaction.Cancel(); OutError = {TEXT("invalid_row"), TEXT("The row rename source or destination is invalid")}; return false; }
            ChangedNames.Add(New);
        }
        else if (Operation == TEXT("remove_row"))
        {
            FString Name;
            if (!Arguments->TryGetStringField(TEXT("row_name"), Name) || !RequestValidation::ValidName(Name) || !FDataTableEditorUtils::RemoveRow(Table, FName(*Name)))
            { Transaction.Cancel(); OutError = {TEXT("invalid_row"), TEXT("The row to remove does not exist")}; return false; }
            ChangedNames.Add(Name);
        }
        else
        {
            TArray<TSharedPtr<FJsonValue>> Writes; TArray<FString> Removes;
            if (Operation == TEXT("add_row") || Operation == TEXT("replace_row"))
            {
                const TSharedRef<FJsonObject> Write = MakeShared<FJsonObject>(); FString Name; const TSharedPtr<FJsonObject>* Values = nullptr; bool bPreserve = false;
                if (!Arguments->TryGetStringField(TEXT("row_name"), Name) || !Arguments->TryGetObjectField(TEXT("values"), Values) || Values == nullptr
                    || (Arguments->HasField(TEXT("preserve_unspecified")) && !Arguments->TryGetBoolField(TEXT("preserve_unspecified"), bPreserve)))
                { Transaction.Cancel(); OutError = {TEXT("invalid_row"), TEXT("The row write is invalid")}; return false; }
                Write->SetStringField(TEXT("row_name"), Name); Write->SetObjectField(TEXT("values"), *Values); Write->SetBoolField(TEXT("preserve_unspecified"), bPreserve);
                Writes.Add(MakeShared<FJsonValueObject>(Write));
                const bool bExists = Table->FindRowUnchecked(FName(*Name)) != nullptr;
                if ((Operation == TEXT("add_row") && bExists) || (Operation == TEXT("replace_row") && !bExists))
                { Transaction.Cancel(); OutError = {TEXT("invalid_row"), Operation == TEXT("add_row") ? TEXT("The row already exists") : TEXT("The row does not exist")}; return false; }
            }
            else if (Operation == TEXT("batch"))
            {
                const TArray<TSharedPtr<FJsonValue>>* Upserts = nullptr; const TArray<TSharedPtr<FJsonValue>>* RemoveValues = nullptr;
                if (!Arguments->TryGetArrayField(TEXT("upserts"), Upserts) || Upserts == nullptr || !Arguments->TryGetArrayField(TEXT("remove_rows"), RemoveValues) || RemoveValues == nullptr
                    || Upserts->Num() + RemoveValues->Num() > UnrealMCP::MaxGameDataBatchRows)
                { Transaction.Cancel(); OutError = {TEXT("data_limit_exceeded"), TEXT("The atomic row batch exceeds the configured limit")}; return false; }
                Writes = *Upserts; TSet<FName> Seen;
                for (const TSharedPtr<FJsonValue>& Value : *RemoveValues)
                {
                    FString Name; if (!Value->TryGetString(Name) || !RequestValidation::ValidName(Name) || Seen.Contains(FName(*Name)) || Table->FindRowUnchecked(FName(*Name)) == nullptr)
                    { Transaction.Cancel(); OutError = {TEXT("invalid_row"), TEXT("A batch removal is missing, duplicate, or case-conflicting")}; return false; }
                    Seen.Add(FName(*Name)); Removes.Add(Name);
                }
                for (const TSharedPtr<FJsonValue>& Value : Writes)
                {
                    const TSharedPtr<FJsonObject>* Object = nullptr; FString Name;
                    if (!Value->TryGetObject(Object) || Object == nullptr || !(*Object)->TryGetStringField(TEXT("row_name"), Name) || Seen.Contains(FName(*Name)))
                    { Transaction.Cancel(); OutError = {TEXT("invalid_row"), TEXT("Batch upserts and removals overlap or conflict")}; return false; }
                    Seen.Add(FName(*Name));
                }
            }
            else { Transaction.Cancel(); OutError = {TEXT("invalid_argument"), TEXT("Unknown Data Table row operation")}; return false; }
            TArray<FStagedRow> Staged; if (!StageRows(Struct, Writes, Table, Staged, OutError)) { Transaction.Cancel(); return false; }
            for (const FString& Name : Removes) { if (!FDataTableEditorUtils::RemoveRow(Table, FName(*Name))) { Transaction.Cancel(); OutError = {TEXT("internal_error"), TEXT("A prevalidated row removal failed")}; return false; } ChangedNames.Add(Name); }
            FDataTableEditorUtils::BroadcastPreChange(Table, FDataTableEditorUtils::EDataTableChangeInfo::RowData); Table->Modify();
            for (const FStagedRow& Row : Staged)
            {
                uint8* Destination = Table->FindRowUnchecked(FName(*Row.Name));
                if (Destination == nullptr) Destination = FDataTableEditorUtils::AddRow(Table, FName(*Row.Name));
                if (Destination == nullptr) { Transaction.Cancel(); OutError = {TEXT("internal_error"), TEXT("A prevalidated row upsert failed")}; return false; }
                Struct->CopyScriptStruct(Destination, Row.Data->GetStructMemory()); Table->HandleDataTableChanged(FName(*Row.Name)); ChangedNames.Add(Row.Name);
            }
            FDataTableEditorUtils::BroadcastPostChange(Table, FDataTableEditorUtils::EDataTableChangeInfo::RowData);
        }
    }
    if (!SaveAsset(Asset))
    {
        Transaction.Cancel(); OutError = {TEXT("save_failed"), TEXT("The game-data mutation could not be saved")}; RestoreAfterFailure(Asset, OutError); return false;
    }
    FString AfterTarget, AfterObject, AfterPackage, AfterSnapshot; TArray<TSharedPtr<FJsonValue>> AfterRecords, AfterSchema; TSharedPtr<FJsonObject> AfterMetadata;
    if (!UnrealMCP::GameDataInspectionBuilder::Build(
            *InspectArgs, AfterTarget, AfterObject, AfterPackage, AfterRecords, AfterSchema, AfterSnapshot, AfterMetadata, OutError)
        || AfterSnapshot == BeforeSnapshot)
    { Transaction.Cancel(); if (OutError.Code.IsEmpty()) OutError = {TEXT("internal_error"), TEXT("Game-data read-back did not verify a changed snapshot")}; RestoreAfterFailure(Asset, OutError); return false; }
    const TSharedRef<FJsonObject> Result = InspectionBuilder::BuildEditResult(Target, AfterObject, AfterSnapshot); Result->SetStringField(TEXT("operation"), Operation);
    TArray<TSharedPtr<FJsonValue>> Names; for (const FString& Name : ChangedNames) Names.Add(MakeShared<FJsonValueString>(Name));
    Result->SetArrayField(TEXT("changed_names"), Names); Result->SetNumberField(TEXT("changed_count"), ChangedNames.Num()); OutResult = Result; return true;
}
