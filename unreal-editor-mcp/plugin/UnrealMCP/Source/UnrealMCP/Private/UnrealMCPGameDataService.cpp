#include "UnrealMCPGameDataService.h"

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
struct FStagedRow
{
    FString Name;
    bool bPreserve = false;
    TSharedPtr<FStructOnScope> Data;
};

bool GameDataHasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed)
{
    TSet<FString> Names;
    for (const TCHAR* Name : Allowed) Names.Add(Name);
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values) if (!Names.Contains(Pair.Key)) return false;
    return true;
}

bool ValidateEditShape(const FJsonObject& Arguments, const FString& Target, const FString& Operation, FUnrealMCPError& OutError)
{
    bool bValid = false;
    if (Target == TEXT("user_defined_struct"))
    {
        if (Operation == TEXT("create"))
            bValid = GameDataHasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("members")});
        else if (Operation == TEXT("add_member"))
            bValid = GameDataHasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("member")});
        else if (Operation == TEXT("rename_member"))
            bValid = GameDataHasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("member_id"), TEXT("new_name")});
        else if (Operation == TEXT("reorder_member"))
        {
            FString Position;
            bValid = GameDataHasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("member_id"), TEXT("relative_to_member_id"), TEXT("position")})
                && Arguments.TryGetStringField(TEXT("position"), Position)
                && (Position == TEXT("above") || Position == TEXT("below"));
        }
        else if (Operation == TEXT("remove_member"))
        {
            FString Policy;
            bValid = GameDataHasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("member_id"), TEXT("policy")})
                && Arguments.TryGetStringField(TEXT("policy"), Policy) && Policy == TEXT("reject_if_referenced");
        }
        else if (Operation == TEXT("update_member"))
        {
            FString Field;
            FString Policy;
            bValid = Arguments.TryGetStringField(TEXT("field"), Field)
                && (Field == TEXT("type")
                    ? GameDataHasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("member_id"), TEXT("field"), TEXT("type"), TEXT("policy")})
                        && Arguments.TryGetStringField(TEXT("policy"), Policy) && Policy == TEXT("reject_if_referenced")
                    : Field == TEXT("default") && GameDataHasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("member_id"), TEXT("field"), TEXT("default")}));
        }
    }
    else if (Target == TEXT("data_table"))
    {
        if (Operation == TEXT("create"))
            bValid = GameDataHasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("row_struct"), TEXT("rows")});
        else if (Operation == TEXT("add_row"))
            bValid = GameDataHasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("row_name"), TEXT("values")});
        else if (Operation == TEXT("replace_row"))
            bValid = GameDataHasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("row_name"), TEXT("values"), TEXT("preserve_unspecified")});
        else if (Operation == TEXT("rename_row"))
            bValid = GameDataHasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("row_name"), TEXT("new_row_name")});
        else if (Operation == TEXT("remove_row"))
            bValid = GameDataHasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("row_name")});
        else if (Operation == TEXT("batch"))
            bValid = GameDataHasOnlyFields(Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("upserts"), TEXT("remove_rows")});
    }
    if (!bValid) OutError = {TEXT("invalid_argument"), TEXT("game_data_edit accepts only its exact target and operation fields")};
    return bValid;
}

FString ObjectPathForPackage(const FString& PackageName)
{
    return PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
}

bool NormalizePackagePath(const FString& Input, FString& OutPackage)
{
    OutPackage = Input;
    return Input.StartsWith(TEXT("/")) && !Input.StartsWith(TEXT("//")) && !Input.EndsWith(TEXT("/"))
        && !Input.Contains(TEXT("..")) && !Input.Contains(TEXT("\\")) && !Input.Contains(TEXT("."))
        && Input.Len() <= 512 && FPackageName::IsValidLongPackageName(Input, true)
        && !FPackageName::GetLongPackageAssetName(Input).IsEmpty();
}

bool NormalizeAssetPath(const FString& Input, FString& OutObject, FString& OutPackage)
{
    if (!Input.StartsWith(TEXT("/")) || Input.StartsWith(TEXT("//")) || Input.Contains(TEXT(".."))
        || Input.Contains(TEXT("\\")) || Input.Len() > 512) return false;
    OutPackage = FPackageName::ObjectPathToPackageName(Input);
    if (!FPackageName::IsValidLongPackageName(OutPackage, true)) return false;
    OutObject = Input.Contains(TEXT(".")) ? Input : ObjectPathForPackage(OutPackage);
    return FPackageName::IsValidObjectPath(OutObject)
        && FPackageName::ObjectPathToObjectName(OutObject) == FPackageName::GetLongPackageAssetName(OutPackage);
}

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
    if (FindPackage(nullptr, *PackageName) != nullptr || FindObject<UObject>(nullptr, *ObjectPathForPackage(PackageName)) != nullptr) return true;
    FString Filename;
    if (FPackageName::DoesPackageExist(PackageName, &Filename) || IFileManager::Get().FileExists(*Filename)) return true;
    const FAssetData Existing = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
        .Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPathForPackage(PackageName)));
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

bool ReadPageSize(const FJsonObject& Arguments, int32& Out, FUnrealMCPError& OutError)
{
    Out = UnrealMCP::DefaultInspectPageSize;
    if (Arguments.HasField(TEXT("page_size")))
    {
        double Value = 0.0;
        if (!Arguments.TryGetNumberField(TEXT("page_size"), Value) || Value != FMath::FloorToDouble(Value)
            || Value < 1 || Value > UnrealMCP::MaxInspectPageSize)
        { OutError = {TEXT("invalid_argument"), TEXT("page_size must be a bounded integer")}; return false; }
        Out = static_cast<int32>(Value);
    }
    return true;
}

FString HashJson(const TSharedRef<FJsonObject>& Value)
{
    FString Text; const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text); FJsonSerializer::Serialize(Value, Writer);
    FTCHARToUTF8 Encoded(*Text); uint8 Digest[FSHA1::DigestSize]; FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

FString Guid(const FGuid& Value) { return Value.IsValid() ? Value.ToString(EGuidFormats::Digits).ToLower() : FString(); }

bool ParseGuidField(const FJsonObject& Arguments, const TCHAR* Name, FGuid& Out, FUnrealMCPError& OutError)
{
    FString Text;
    if (!Arguments.TryGetStringField(Name, Text) || !FGuid::ParseExact(Text, EGuidFormats::Digits, Out))
    { OutError = {TEXT("invalid_argument"), FString(Name) + TEXT(" must be one stable 32-character identity")}; return false; }
    return true;
}

bool ValidName(const FString& Value)
{
    return !Value.IsEmpty() && Value.Len() <= 128 && FName::IsValidXName(Value, INVALID_NAME_CHARACTERS)
        && !Value.Equals(TEXT("None"), ESearchCase::IgnoreCase);
}

bool GatherDependencies(const FString& PackageName, TArray<FString>& Out, bool& bTruncated)
{
    TArray<FName> Referencers;
    FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetReferencers(
        FName(*PackageName), Referencers, UE::AssetRegistry::EDependencyCategory::Package);
    Referencers.Sort(FNameLexicalLess());
    bTruncated = Referencers.Num() > UnrealMCP::MaxGameDataDependencies;
    for (int32 Index = 0; Index < FMath::Min(Referencers.Num(), UnrealMCP::MaxGameDataDependencies); ++Index) Out.Add(Referencers[Index].ToString());
    return true;
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
    if (!Object.IsValid() || !GameDataHasOnlyFields(*Object, {TEXT("name"), TEXT("type"), TEXT("default"), TEXT("tooltip")})
        || !Object->TryGetStringField(TEXT("name"), OutName) || !ValidName(OutName)
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

TSharedRef<FJsonObject> SchemaRecord(const UScriptStruct* Struct, const FProperty* Property)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), Struct->GetAuthoredNameForField(Property));
    Result->SetStringField(TEXT("property_name"), Property->GetName());
    Result->SetObjectField(TEXT("type"), UnrealMCP::GameDataValueCodec::EncodeType(Property));
    return Result;
}

bool BuildInspection(const FJsonObject& Arguments, FString& OutTarget, FString& OutObjectPath, FString& OutPackage,
    TArray<TSharedPtr<FJsonValue>>& OutRecords, TArray<TSharedPtr<FJsonValue>>& OutSchema, FString& OutSnapshot,
    TSharedPtr<FJsonObject>& OutMetadata, FUnrealMCPError& OutError)
{
    FString InputPath;
    if (!Arguments.TryGetStringField(TEXT("target"), OutTarget)
        || (OutTarget != TEXT("user_defined_struct") && OutTarget != TEXT("data_table"))
        || !Arguments.TryGetStringField(TEXT("asset_path"), InputPath) || !NormalizeAssetPath(InputPath, OutObjectPath, OutPackage))
    { OutError = {TEXT("invalid_argument"), TEXT("target and asset_path must identify one supported game-data asset")}; return false; }
    if ((OutTarget == TEXT("user_defined_struct")
            && !GameDataHasOnlyFields(Arguments, {TEXT("target"), TEXT("asset_path"), TEXT("page_size")}))
        || (OutTarget == TEXT("data_table")
            && !GameDataHasOnlyFields(Arguments, {TEXT("target"), TEXT("asset_path"), TEXT("row_names"), TEXT("page_size")})))
    { OutError = {TEXT("invalid_argument"), TEXT("game_data_inspect accepts only fields for its exact target")}; return false; }
    UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *OutObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (Asset == nullptr) { OutError = {TEXT("not_found"), TEXT("The game-data asset was not found")}; return false; }
    OutMetadata = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> FingerprintRecords;
    if (OutTarget == TEXT("user_defined_struct"))
    {
        UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(Asset);
        if (Struct == nullptr) { OutError = {TEXT("wrong_type"), TEXT("The asset is not a user-defined struct")}; return false; }
        OutMetadata->SetStringField(TEXT("compile_state"), Struct->Status == UDSS_UpToDate ? TEXT("up_to_date") : Struct->Status == UDSS_Error ? TEXT("error") : TEXT("dirty"));
        const TArray<FStructVariableDescription>& Members = FStructureEditorUtils::GetVarDesc(Struct);
        if (Members.Num() > UnrealMCP::MaxGameDataFields) { OutError = {TEXT("data_limit_exceeded"), TEXT("The struct exceeds the member limit")}; return false; }
        for (int32 Index = 0; Index < Members.Num(); ++Index)
        {
            const FStructVariableDescription& Member = Members[Index];
            const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>(); Record->SetStringField(TEXT("kind"), TEXT("member"));
            Record->SetStringField(TEXT("id"), Guid(Member.VarGuid)); Record->SetBoolField(TEXT("identity_stable"), Member.VarGuid.IsValid());
            Record->SetStringField(TEXT("name"), Member.FriendlyName); Record->SetStringField(TEXT("property_name"), Member.VarName.ToString());
            Record->SetNumberField(TEXT("order"), Index); const FEdGraphPinType PinType = Member.ToPinType();
            Record->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(PinType));
            Record->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(PinType, Member.DefaultValue));
            Record->SetStringField(TEXT("tooltip"), Member.ToolTip.Left(512));
            const TSharedPtr<FJsonValue> Value = MakeShared<FJsonValueObject>(Record); OutRecords.Add(Value); FingerprintRecords.Add(Value);
        }
        TArray<FString> Dependencies; bool bTruncated = false; GatherDependencies(OutPackage, Dependencies, bTruncated);
        TArray<TSharedPtr<FJsonValue>> Values; for (const FString& Item : Dependencies) Values.Add(MakeShared<FJsonValueString>(Item));
        OutMetadata->SetArrayField(TEXT("dependencies"), Values); OutMetadata->SetBoolField(TEXT("dependencies_truncated"), bTruncated);
    }
    else
    {
        UDataTable* Table = Cast<UDataTable>(Asset);
        if (Table == nullptr || Table->GetRowStruct() == nullptr) { OutError = {TEXT("wrong_type"), TEXT("The asset is not a valid Data Table")}; return false; }
        const UScriptStruct* RowStruct = Table->GetRowStruct(); OutMetadata->SetStringField(TEXT("row_struct"), RowStruct->GetPathName());
        OutMetadata->SetStringField(TEXT("row_struct_kind"), RowStruct->IsA<UUserDefinedStruct>() ? TEXT("user_defined") : TEXT("native"));
        int32 Fields = 0; for (TFieldIterator<FProperty> It(RowStruct); It; ++It)
        {
            if (++Fields > UnrealMCP::MaxGameDataFields) { OutError = {TEXT("data_limit_exceeded"), TEXT("The row schema exceeds the field limit")}; return false; }
            OutSchema.Add(MakeShared<FJsonValueObject>(SchemaRecord(RowStruct, *It)));
        }
        TSet<FName> Filter;
        if (Arguments.HasField(TEXT("row_names")))
        {
            const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
            if (!Arguments.TryGetArrayField(TEXT("row_names"), Names) || Names == nullptr || Names->IsEmpty() || Names->Num() > UnrealMCP::MaxGameDataBatchRows)
            { OutError = {TEXT("invalid_argument"), TEXT("row_names must be one bounded non-empty array")}; return false; }
            for (const TSharedPtr<FJsonValue>& Value : *Names)
            {
                FString Name; if (!Value->TryGetString(Name) || !ValidName(Name) || Filter.Contains(FName(*Name)))
                { OutError = {TEXT("invalid_argument"), TEXT("row_names contains an invalid, duplicate, or case-conflicting name")}; return false; }
                Filter.Add(FName(*Name));
            }
        }
        if (Table->GetRowMap().Num() > UnrealMCP::MaxGameDataRows) { OutError = {TEXT("data_limit_exceeded"), TEXT("The Data Table exceeds the row scan limit")}; return false; }
        TArray<FName> Names; Table->GetRowMap().GenerateKeyArray(Names); Names.Sort(FNameLexicalLess());
        for (const FName Name : Names)
        {
            bool bEncoded = false; FUnrealMCPError ValueError;
            const TSharedRef<FJsonObject> Values = UnrealMCP::GameDataValueCodec::EncodeFields(RowStruct, Table->FindRowUnchecked(Name), 0, ValueError, bEncoded);
            if (!bEncoded) { OutError = ValueError; return false; }
            const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>(); Record->SetStringField(TEXT("kind"), TEXT("row"));
            Record->SetStringField(TEXT("name"), Name.ToString()); Record->SetObjectField(TEXT("values"), Values);
            const TSharedPtr<FJsonValue> Value = MakeShared<FJsonValueObject>(Record); FingerprintRecords.Add(Value);
            if (Filter.IsEmpty() || Filter.Contains(Name)) OutRecords.Add(Value);
        }
        if (!Filter.IsEmpty() && OutRecords.Num() != Filter.Num()) { OutError = {TEXT("invalid_row"), TEXT("One or more requested rows do not exist")}; return false; }
        OutMetadata->SetNumberField(TEXT("row_count"), Table->GetRowMap().Num());
    }
    const TSharedRef<FJsonObject> Fingerprint = MakeShared<FJsonObject>(); Fingerprint->SetStringField(TEXT("target"), OutTarget);
    Fingerprint->SetStringField(TEXT("asset_path"), OutObjectPath); Fingerprint->SetObjectField(TEXT("metadata"), OutMetadata);
    Fingerprint->SetArrayField(TEXT("schema"), OutSchema); Fingerprint->SetArrayField(TEXT("records"), FingerprintRecords);
    OutSnapshot = HashJson(Fingerprint); return true;
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

TSharedRef<FJsonObject> ResultBase(const FString& Target, const FString& ObjectPath, const FString& Snapshot)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("target"), Target);
    Result->SetStringField(TEXT("asset_path"), ObjectPath); Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    Result->SetBoolField(TEXT("saved"), true); Result->SetBoolField(TEXT("dirty"), false); return Result;
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
            || !GameDataHasOnlyFields(**Object, {TEXT("row_name"), TEXT("values"), TEXT("preserve_unspecified")})
            || !(*Object)->TryGetStringField(TEXT("row_name"), Name) || !ValidName(Name)
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

FUnrealMCPGameDataService::FUnrealMCPGameDataService(TFunction<double()> InNow) : Now(MoveTemp(InNow)) {}

void FUnrealMCPGameDataService::RemoveExpired(double CurrentTime)
{
    for (auto It = Cursors.CreateIterator(); It; ++It) if (It.Value().ExpiresAt <= CurrentTime) It.RemoveCurrent();
}

bool FUnrealMCPGameDataService::Inspect(const TSharedPtr<FJsonObject>& Arguments, TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (!Arguments.IsValid()) { OutError = {TEXT("invalid_argument"), TEXT("arguments must be an object")}; return false; }
    RemoveExpired(Now());
    if (!Arguments->HasField(TEXT("cursor"))) return InspectInitial(Arguments, 0, FString(), INDEX_NONE, OutResult, OutError);
    if (!GameDataHasOnlyFields(*Arguments, {TEXT("cursor"), TEXT("page_size")})) { OutError = {TEXT("invalid_argument"), TEXT("Cursor continuation accepts only cursor and page_size")}; return false; }
    FString Cursor; if (!Arguments->TryGetStringField(TEXT("cursor"), Cursor) || Cursor.Len() != 32) { OutError = {TEXT("invalid_argument"), TEXT("cursor must be one opaque identity")}; return false; }
    FCursorState* State = Cursors.Find(Cursor); if (State == nullptr) { OutError = {TEXT("cursor_expired"), TEXT("The game-data cursor is missing or expired"), MakeShared<FJsonObject>(), true}; return false; }
    int32 PageSize = 0; if (!ReadPageSize(*Arguments, PageSize, OutError)) return false;
    const FCursorState Saved = *State; Cursors.Remove(Cursor);
    return InspectInitial(Saved.Arguments, Saved.Offset, Saved.Snapshot, PageSize, OutResult, OutError);
}

bool FUnrealMCPGameDataService::InspectInitial(const TSharedPtr<FJsonObject>& Arguments, int32 Offset, const FString& ExpectedSnapshot,
    int32 PageSizeOverride, TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError)
{
    int32 PageSize = 0; if (!ReadPageSize(*Arguments, PageSize, OutError)) return false; if (PageSizeOverride != INDEX_NONE) PageSize = PageSizeOverride;
    FString Target, ObjectPath, Package, Snapshot; TArray<TSharedPtr<FJsonValue>> Records, Schema; TSharedPtr<FJsonObject> Metadata;
    if (!BuildInspection(*Arguments, Target, ObjectPath, Package, Records, Schema, Snapshot, Metadata, OutError)) return false;
    if (!ExpectedSnapshot.IsEmpty() && ExpectedSnapshot != Snapshot) { OutError = {TEXT("stale_precondition"), TEXT("The game-data snapshot changed before cursor continuation")}; return false; }
    if (Offset < 0 || Offset > Records.Num()) { OutError = {TEXT("cursor_expired"), TEXT("The cursor no longer identifies a valid page")}; return false; }
    const int32 End = FMath::Min(Offset + PageSize, Records.Num()); TArray<TSharedPtr<FJsonValue>> Page;
    for (int32 Index = Offset; Index < End; ++Index) Page.Add(Records[Index]);
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("target"), Target); Result->SetStringField(TEXT("asset_path"), ObjectPath);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot); Result->SetObjectField(TEXT("metadata"), Metadata); Result->SetArrayField(TEXT("schema"), Schema);
    Result->SetArrayField(TEXT("records"), Page); Result->SetNumberField(TEXT("record_count"), Records.Num()); Result->SetNumberField(TEXT("page_offset"), Offset); Result->SetBoolField(TEXT("has_more"), End < Records.Num());
    if (End < Records.Num())
    {
        RemoveExpired(Now());
        if (Cursors.Num() >= UnrealMCP::MaxRetainedCursors)
        {
            FString Oldest; double Expiry = TNumericLimits<double>::Max();
            for (const TPair<FString, FCursorState>& Pair : Cursors) if (Pair.Value.ExpiresAt < Expiry) { Expiry = Pair.Value.ExpiresAt; Oldest = Pair.Key; }
            Cursors.Remove(Oldest);
        }
        const FString Cursor = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        Cursors.Add(Cursor, FCursorState{Arguments, Snapshot, End, Now() + UnrealMCP::CursorLifetimeSeconds});
        Result->SetStringField(TEXT("next_cursor"), Cursor); Result->SetNumberField(TEXT("cursor_expires_in_ms"), static_cast<int32>(UnrealMCP::CursorLifetimeSeconds * 1000.0));
    }
    OutResult = Result; return true;
}

bool FUnrealMCPGameDataService::Edit(const TSharedPtr<FJsonObject>& Arguments, TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    FString Target, Operation, InputPath;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("target"), Target) || !Arguments->TryGetStringField(TEXT("operation"), Operation)
        || !Arguments->TryGetStringField(TEXT("asset_path"), InputPath)
        || (Target != TEXT("user_defined_struct") && Target != TEXT("data_table")))
    { OutError = {TEXT("invalid_argument"), TEXT("game_data_edit requires one exact target, operation, and asset_path")}; return false; }
    if (!ValidateEditShape(*Arguments, Target, Operation, OutError)) return false;

    if (Operation == TEXT("create"))
    {
        FString PackageName;
        if (!NormalizePackagePath(InputPath, PackageName))
        { OutError = {TEXT("invalid_argument"), TEXT("create asset_path must be one exact bounded Unreal package path")}; return false; }
        if (!ValidateMutationScope(PackageName, OutError)) return false;
        if (PackageExists(PackageName))
        { OutError = {TEXT("already_exists"), TEXT("The destination package or asset already exists")}; return false; }
        FString Filename; if (!WritableFilename(PackageName, Filename, OutError)) return false;
        const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName); UPackage* Package = CreatePackage(*PackageName); UObject* Asset = nullptr;
        if (Target == TEXT("user_defined_struct"))
        {
            const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
            if (!GameDataHasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("members")})
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
            if (!GameDataHasOnlyFields(*Arguments, {TEXT("operation_id"), TEXT("target"), TEXT("operation"), TEXT("asset_path"), TEXT("row_struct"), TEXT("rows")})
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
        if (!BuildInspection(*InspectArgs, ActualTarget, ObjectPath, PackagePath, Records, Schema, Snapshot, Metadata, OutError)) { CleanupCreation(Package, Asset, Filename, true); return false; }
        const TSharedRef<FJsonObject> Result = ResultBase(Target, ObjectPath, Snapshot); Result->SetStringField(TEXT("operation"), TEXT("create"));
        Result->SetNumberField(TEXT("changed_count"), Records.Num()); OutResult = Result; return true;
    }

    FString ObjectPath, PackageName;
    if (!NormalizeAssetPath(InputPath, ObjectPath, PackageName)) { OutError = {TEXT("invalid_argument"), TEXT("asset_path must be one exact bounded Unreal asset path")}; return false; }
    if (!ValidateMutationScope(PackageName, OutError)) return false;
    const TSharedRef<FJsonObject> InspectArgs = MakeShared<FJsonObject>(); InspectArgs->SetStringField(TEXT("target"), Target); InspectArgs->SetStringField(TEXT("asset_path"), ObjectPath);
    FString ActualTarget, ActualObject, ActualPackage, BeforeSnapshot; TArray<TSharedPtr<FJsonValue>> BeforeRecords, BeforeSchema; TSharedPtr<FJsonObject> BeforeMetadata;
    if (!BuildInspection(*InspectArgs, ActualTarget, ActualObject, ActualPackage, BeforeRecords, BeforeSchema, BeforeSnapshot, BeforeMetadata, OutError)
        || !ValidateExpected(*Arguments, BeforeSnapshot, OutError)) return false;
    UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
    FScopedTransaction Transaction(NSLOCTEXT("UnrealMCP", "GameDataEdit", "Unreal MCP Game Data Edit")); Asset->Modify();
    TArray<FString> ChangedNames;
    if (Target == TEXT("user_defined_struct"))
    {
        UUserDefinedStruct* Struct = CastChecked<UUserDefinedStruct>(Asset); FGuid MemberId; FStructVariableDescription* Member = nullptr; FString ExistingMemberName;
        if (Operation != TEXT("add_member"))
        {
            if (!ParseGuidField(*Arguments, TEXT("member_id"), MemberId, OutError)) { Transaction.Cancel(); return false; }
            Member = FStructureEditorUtils::GetVarDescByGuid(Struct, MemberId);
            if (Member == nullptr) { Transaction.Cancel(); OutError = {TEXT("invalid_schema"), TEXT("The struct member identity is missing or stale")}; return false; }
            ExistingMemberName = Member->FriendlyName;
        }
        bool bChanged = false;
        if (Operation == TEXT("rename_member"))
        {
            FString Name; bChanged = Arguments->TryGetStringField(TEXT("new_name"), Name) && ValidName(Name) && FStructureEditorUtils::RenameVariable(Struct, MemberId, Name); ChangedNames.Add(Name);
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
                TArray<FString> Dependencies; bool bTruncated = false; GatherDependencies(PackageName, Dependencies, bTruncated);
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
            bChanged = ParseGuidField(*Arguments, TEXT("relative_to_member_id"), Relative, OutError)
                && Arguments->TryGetStringField(TEXT("position"), Position) && MemberId != Relative
                && FStructureEditorUtils::MoveVariable(Struct, MemberId, Relative,
                    Position == TEXT("above") ? FStructureEditorUtils::PositionAbove : FStructureEditorUtils::PositionBelow);
            ChangedNames.Add(ExistingMemberName);
        }
        else if (Operation == TEXT("remove_member"))
        {
            TArray<FString> Dependencies; bool bTruncated = false; GatherDependencies(PackageName, Dependencies, bTruncated);
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
            if (!Arguments->TryGetStringField(TEXT("row_name"), Old) || !Arguments->TryGetStringField(TEXT("new_row_name"), New) || !ValidName(Old) || !ValidName(New)
                || Table->FindRowUnchecked(FName(*Old)) == nullptr || Table->FindRowUnchecked(FName(*New)) != nullptr || !FDataTableEditorUtils::RenameRow(Table, FName(*Old), FName(*New)))
            { Transaction.Cancel(); OutError = {TEXT("invalid_row"), TEXT("The row rename source or destination is invalid")}; return false; }
            ChangedNames.Add(New);
        }
        else if (Operation == TEXT("remove_row"))
        {
            FString Name;
            if (!Arguments->TryGetStringField(TEXT("row_name"), Name) || !ValidName(Name) || !FDataTableEditorUtils::RemoveRow(Table, FName(*Name)))
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
                    FString Name; if (!Value->TryGetString(Name) || !ValidName(Name) || Seen.Contains(FName(*Name)) || Table->FindRowUnchecked(FName(*Name)) == nullptr)
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
    if (!BuildInspection(*InspectArgs, AfterTarget, AfterObject, AfterPackage, AfterRecords, AfterSchema, AfterSnapshot, AfterMetadata, OutError) || AfterSnapshot == BeforeSnapshot)
    { Transaction.Cancel(); if (OutError.Code.IsEmpty()) OutError = {TEXT("internal_error"), TEXT("Game-data read-back did not verify a changed snapshot")}; RestoreAfterFailure(Asset, OutError); return false; }
    const TSharedRef<FJsonObject> Result = ResultBase(Target, AfterObject, AfterSnapshot); Result->SetStringField(TEXT("operation"), Operation);
    TArray<TSharedPtr<FJsonValue>> Names; for (const FString& Name : ChangedNames) Names.Add(MakeShared<FJsonValueString>(Name));
    Result->SetArrayField(TEXT("changed_names"), Names); Result->SetNumberField(TEXT("changed_count"), ChangedNames.Num()); OutResult = Result; return true;
}
