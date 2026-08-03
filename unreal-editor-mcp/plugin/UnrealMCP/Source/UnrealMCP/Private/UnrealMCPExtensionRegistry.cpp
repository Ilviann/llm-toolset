#include "UnrealMCPExtensionRegistry.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "PluginDescriptor.h"
#include "ScopedTransaction.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UnrealMCPProtocol.h"
#include "UnrealMCPVersion.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
constexpr TCHAR CompanionMetadataField[] = TEXT("unreal_mcp_companion");

bool ReadPositiveInteger(const FJsonObject& Object, const TCHAR* Field, int32& OutValue)
{
    double Number = 0.0;
    if (!Object.TryGetNumberField(Field, Number)
        || !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
        || Number < 1.0 || Number > static_cast<double>(MAX_int32))
    {
        return false;
    }
    OutValue = static_cast<int32>(Number);
    return true;
}

bool ReadStringArray(const FJsonObject& Object, const TCHAR* Field, TArray<FString>& OutValues)
{
    OutValues.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object.TryGetArrayField(Field, Values) || Values == nullptr)
    {
        return false;
    }
    if (Values->Num() > 32) return false;
    TSet<FString> Seen;
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        FString Text;
        if (!Value.IsValid() || !Value->TryGetString(Text) || Text.IsEmpty() || Text.Len() > 64)
        {
            return false;
        }
        if (Seen.Contains(Text)) return false;
        Seen.Add(Text);
        OutValues.Add(Text);
    }
    OutValues.Sort();
    return true;
}

bool IsSemanticVersion(const FString& Value)
{
    if (Value.IsEmpty() || Value.Len() > 32) return false;
    FString CoreAndPrerelease = Value;
    FString Build;
    if (Value.Split(TEXT("+"), &CoreAndPrerelease, &Build))
    {
        if (Build.IsEmpty() || Build.Contains(TEXT("+"))) return false;
    }
    FString Core = CoreAndPrerelease;
    FString Prerelease;
    if (CoreAndPrerelease.Split(TEXT("-"), &Core, &Prerelease))
    {
        if (Prerelease.IsEmpty()) return false;
    }
    TArray<FString> Parts;
    Core.ParseIntoArray(Parts, TEXT("."), false);
    if (Parts.Num() != 3) return false;
    for (const FString& Part : Parts)
    {
        if (Part.IsEmpty() || (Part.Len() > 1 && Part[0] == TEXT('0'))) return false;
        for (const TCHAR Character : Part)
        {
            if (Character < TEXT('0') || Character > TEXT('9')) return false;
        }
    }
    for (const FString& Suffix : {Prerelease, Build})
    {
        if (Suffix.IsEmpty()) continue;
        TArray<FString> Identifiers;
        Suffix.ParseIntoArray(Identifiers, TEXT("."), false);
        if (Identifiers.IsEmpty()) return false;
        for (const FString& Identifier : Identifiers)
        {
            if (Identifier.IsEmpty()) return false;
            for (const TCHAR Character : Identifier)
            {
                if (!FChar::IsAlnum(Character) && Character != TEXT('-')) return false;
            }
        }
    }
    return true;
}

bool SameStringSet(TArray<FString> Left, TArray<FString> Right)
{
    Left.Sort();
    Right.Sort();
    return Left == Right;
}

bool IsNativeName(const FString& Value)
{
    if (Value.IsEmpty() || Value.Len() > 64) return false;
    for (const TCHAR Character : Value)
    {
        if (!FChar::IsAlnum(Character) && Character != TEXT('_')) return false;
    }
    return true;
}

bool AreNativeNames(const TArray<FString>& Values)
{
    for (const FString& Value : Values)
    {
        if (!IsNativeName(Value)) return false;
    }
    return true;
}

FString CategoryName(EUnrealMCPExtensionCategory Category)
{
    switch (Category)
    {
    case EUnrealMCPExtensionCategory::AssetFamily: return TEXT("asset_family");
    case EUnrealMCPExtensionCategory::ComponentFamily: return TEXT("component_family");
    default: return TEXT("existing_asset_contributor");
    }
}

FString AccessName(EUnrealMCPExtensionAccess Access)
{
    return Access == EUnrealMCPExtensionAccess::Mutation ? TEXT("mutation") : TEXT("read");
}

FString PersistenceName(EUnrealMCPExtensionPersistence Persistence)
{
    switch (Persistence)
    {
    case EUnrealMCPExtensionPersistence::PackageSave: return TEXT("package_save");
    case EUnrealMCPExtensionPersistence::BlueprintCompileAndSave:
        return TEXT("blueprint_compile_and_save");
    default: return TEXT("none");
    }
}

TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& Values)
{
    TArray<TSharedPtr<FJsonValue>> Result;
    for (const FString& Value : Values)
    {
        Result.Add(MakeShared<FJsonValueString>(Value));
    }
    return Result;
}

bool PathContainsSymlink(const FString& Root, const FString& Candidate)
{
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    FString Current = Root;
    FPaths::NormalizeDirectoryName(Current);
    if (PlatformFile.IsSymlink(*Current) == ESymlinkResult::Symlink)
    {
        return true;
    }
    FString Relative = Candidate;
    FPaths::NormalizeDirectoryName(Relative);
    if (!FPaths::MakePathRelativeTo(Relative, *(Current + TEXT("/"))))
    {
        return true;
    }
    TArray<FString> Segments;
    Relative.ParseIntoArray(Segments, TEXT("/"), true);
    for (const FString& Segment : Segments)
    {
        Current /= Segment;
        if ((PlatformFile.FileExists(*Current) || PlatformFile.DirectoryExists(*Current))
            && PlatformFile.IsSymlink(*Current) == ESymlinkResult::Symlink)
        {
            return true;
        }
    }
    return false;
}
}

bool FUnrealMCPExtensionRegistry::IsStableId(const FString& Value)
{
    if (Value.IsEmpty() || Value.Len() > UnrealMCP::MaxExtensionIdChars)
    {
        return false;
    }
    for (const TCHAR Character : Value)
    {
        if (!((Character >= TEXT('a') && Character <= TEXT('z'))
            || (Character >= TEXT('0') && Character <= TEXT('9'))
            || Character == TEXT('-') || Character == TEXT('_')))
        {
            return false;
        }
    }
    return true;
}

void FUnrealMCPExtensionRegistry::DiscoverAndLoad()
{
    check(IsInGameThread());
    Descriptors.Reset();
    Diagnostics.Reset();

    IPluginManager& PluginManager = IPluginManager::Get();
    int32 SeenCompanions = 0;
    for (const TSharedRef<IPlugin>& Plugin : PluginManager.GetDiscoveredPlugins())
    {
        const TSharedPtr<FJsonObject>& DescriptorJson = Plugin->GetDescriptorJson();
        const TSharedPtr<FJsonObject>* Metadata = nullptr;
        if (!DescriptorJson.IsValid()
            || !DescriptorJson->TryGetObjectField(CompanionMetadataField, Metadata)
            || Metadata == nullptr || !Metadata->IsValid())
        {
            continue;
        }
        if (++SeenCompanions > UnrealMCP::MaxDiscoveredCompanions)
        {
            if (Diagnostics.Num() < UnrealMCP::MaxCompanionDiagnostics)
            {
                Diagnostics.Add(TEXT("discovery_limit_exceeded"));
            }
            break;
        }

        FDescriptorRecord Record;
        Record.PluginName = Plugin->GetName();
        Record.SemanticVersion = Plugin->GetDescriptor().VersionName;
        Record.bEnabled = Plugin->IsEnabled();
        const bool bShapeValid = (*Metadata)->Values.Num() == 4
            && (*Metadata)->TryGetStringField(TEXT("extension_id"), Record.ExtensionId)
            && (*Metadata)->TryGetStringField(TEXT("owning_module"), Record.OwningModule)
            && ReadPositiveInteger(*DescriptorJson, TEXT("companion_api_version"), Record.CompanionApiVersion)
            && ReadPositiveInteger(**Metadata, TEXT("schema_revision"), Record.SchemaRevision)
            && ReadStringArray(**Metadata, TEXT("required_engine_plugins"), Record.RequiredEnginePlugins)
            && IsStableId(Record.ExtensionId)
            && IsNativeName(Record.OwningModule)
            && AreNativeNames(Record.RequiredEnginePlugins);
        if (!bShapeValid)
        {
            Record.UnavailableReason = TEXT("invalid_descriptor");
        }
        else if (!Record.bEnabled)
        {
            Record.UnavailableReason = TEXT("disabled");
        }
        else if (!IsSemanticVersion(Record.SemanticVersion))
        {
            Record.UnavailableReason = TEXT("invalid_semantic_version");
        }
        else if (Record.CompanionApiVersion != UnrealMCP::CompanionApiVersion)
        {
            Record.UnavailableReason = TEXT("descriptor_api_mismatch");
        }
        else if (Record.SchemaRevision != UnrealMCP::ExtensionSchemaRevision)
        {
            Record.UnavailableReason = TEXT("unsupported_schema_revision");
        }

        bool bDeclaresBase = false;
        bool bDeclaresModule = false;
        for (const FPluginReferenceDescriptor& Dependency : Plugin->GetDescriptor().Plugins)
        {
            bDeclaresBase |= Dependency.Name == TEXT("UnrealMCP") && Dependency.bEnabled
                && !Dependency.RequestedVersion.IsSet();
        }
        for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
        {
            bDeclaresModule |= Module.Name.ToString() == Record.OwningModule
                && Module.Type == EHostType::Editor;
        }
        if (Record.UnavailableReason.IsEmpty() && !bDeclaresBase)
        {
            Record.UnavailableReason = TEXT("missing_base_dependency");
        }
        if (Record.UnavailableReason.IsEmpty() && !bDeclaresModule)
        {
            Record.UnavailableReason = TEXT("missing_module");
        }
        for (const FString& RequiredPluginName : Record.RequiredEnginePlugins)
        {
            const TSharedPtr<IPlugin> RequiredPlugin = PluginManager.FindPlugin(RequiredPluginName);
            if (Record.UnavailableReason.IsEmpty() && (!RequiredPlugin.IsValid() || !RequiredPlugin->IsEnabled()))
            {
                Record.UnavailableReason = RequiredPlugin.IsValid()
                    ? TEXT("engine_plugin_disabled") : TEXT("engine_plugin_missing");
            }
        }

        Descriptors.Add(MoveTemp(Record));
    }

    Descriptors.Sort([](const FDescriptorRecord& Left, const FDescriptorRecord& Right)
    {
        return Left.ExtensionId < Right.ExtensionId;
    });
    for (int32 Index = 1; Index < Descriptors.Num(); ++Index)
    {
        if (!Descriptors[Index].ExtensionId.IsEmpty()
            && Descriptors[Index - 1].ExtensionId == Descriptors[Index].ExtensionId)
        {
            Descriptors[Index - 1].UnavailableReason = TEXT("duplicate_extension_id");
            Descriptors[Index].UnavailableReason = TEXT("duplicate_extension_id");
        }
    }

    for (FDescriptorRecord& Descriptor : Descriptors)
    {
        if (!Descriptor.bEnabled || !Descriptor.UnavailableReason.IsEmpty())
        {
            continue;
        }
        EModuleLoadResult LoadResult = EModuleLoadResult::Success;
        IModuleInterface* Loaded = FModuleManager::Get().LoadModuleWithFailureReason(
            FName(*Descriptor.OwningModule), LoadResult);
        if (Loaded == nullptr || LoadResult != EModuleLoadResult::Success)
        {
            Descriptor.UnavailableReason = TEXT("startup_failure");
        }
        else if (!Descriptor.bRegistered && Descriptor.UnavailableReason.IsEmpty())
        {
            Descriptor.UnavailableReason = TEXT("registration_missing");
        }
    }
}

FUnrealMCPRegistrationResult FUnrealMCPExtensionRegistry::Register(
    const FUnrealMCPCompanionRegistration& Registration,
    IModuleInterface& OwningModule)
{
    FUnrealMCPRegistrationResult Result;
    FDescriptorRecord* RejectDescriptor = nullptr;
    auto Reject = [this, &Result, &RejectDescriptor](const TCHAR* Reason)
    {
        Result.Reason = Reason;
        if (RejectDescriptor != nullptr)
        {
            RejectDescriptor->UnavailableReason = Reason;
            if (Diagnostics.Num() < UnrealMCP::MaxCompanionDiagnostics)
            {
                Diagnostics.Add((RejectDescriptor->ExtensionId + TEXT(":") + Reason).Left(128));
            }
        }
        return Result;
    };
    if (!IsInGameThread()) return Reject(TEXT("registration_wrong_thread"));
    if (bFrozen || bShuttingDown) return Reject(TEXT("registration_closed"));
    if (Accepted.Num() >= UnrealMCP::MaxAcceptedCompanions) return Reject(TEXT("companion_limit_exceeded"));
    if (!IsStableId(Registration.ExtensionId)
        || Registration.PluginName.IsEmpty() || Registration.PluginName.Len() > 64
        || Registration.OwningModule.IsEmpty() || Registration.OwningModule.Len() > 64
        || !IsSemanticVersion(Registration.SemanticVersion))
    {
        return Reject(TEXT("invalid_identity"));
    }
    FDescriptorRecord* Descriptor = Descriptors.FindByPredicate([&Registration](const FDescriptorRecord& Value)
    {
        return Value.PluginName == Registration.PluginName && Value.ExtensionId == Registration.ExtensionId;
    });
    if (Descriptor == nullptr) return Reject(TEXT("descriptor_not_discovered"));
    RejectDescriptor = Descriptor;
    if (Registration.CompanionApiVersion != UnrealMCP::CompanionApiVersion)
        return Reject(TEXT("compiled_api_mismatch"));
    if (Registration.ExtensionSchemaRevision != UnrealMCP::ExtensionSchemaRevision)
        return Reject(TEXT("unsupported_schema_revision"));
    if (Registration.Contributions.IsEmpty()
        || Registration.Contributions.Num() > UnrealMCP::MaxCompanionContributions)
    {
        return Reject(TEXT("contribution_limit_exceeded"));
    }
    if (!Descriptor->bEnabled || !Descriptor->UnavailableReason.IsEmpty())
        return Reject(TEXT("descriptor_unavailable"));
    if (Descriptor->OwningModule != Registration.OwningModule
        || Descriptor->SemanticVersion != Registration.SemanticVersion
        || Descriptor->CompanionApiVersion != Registration.CompanionApiVersion
        || Descriptor->SchemaRevision != Registration.ExtensionSchemaRevision
        || !SameStringSet(Descriptor->RequiredEnginePlugins, Registration.RequiredEnginePlugins))
    {
        return Reject(TEXT("descriptor_compiled_disagreement"));
    }
    if (FModuleManager::Get().GetModule(FName(*Registration.OwningModule)) != &OwningModule)
        return Reject(TEXT("wrong_module_owner"));
    if (Descriptor->bRegistered) return Reject(TEXT("duplicate_extension_id"));

    static const TSet<FString> ToolFamilies = {
        TEXT("blueprint_inspect"), TEXT("blueprint_action_catalog"), TEXT("blueprint_graph_edit"),
        TEXT("blueprint_create"), TEXT("blueprint_compile"), TEXT("blueprint_save"),
        TEXT("blueprint_component_edit"), TEXT("blueprint_default_edit"), TEXT("blueprint_member_edit"),
        TEXT("widget_tree_edit"), TEXT("game_data_inspect"), TEXT("game_data_edit"),
        TEXT("level_inspect"), TEXT("level_actor_edit"), TEXT("level_save")};
    static const TSet<FString> ReadToolFamilies = {
        TEXT("blueprint_inspect"), TEXT("blueprint_action_catalog"),
        TEXT("game_data_inspect"), TEXT("level_inspect")};
    int32 ExistingCapabilityRecords = 0;
    for (const FAcceptedRecord& Existing : Accepted)
    {
        ExistingCapabilityRecords += Existing.Registration.Contributions.Num();
    }
    if (ExistingCapabilityRecords + Registration.Contributions.Num()
        > UnrealMCP::MaxCompanionCapabilityRecords)
    {
        return Reject(TEXT("capability_record_limit_exceeded"));
    }
    TSet<FString> LocalIds;
    TSet<FString> LocalOperations;
    for (const FUnrealMCPExtensionContribution& Contribution : Registration.Contributions)
    {
        const FString OperationKey = Contribution.ToolFamily + TEXT("|") + Contribution.Operation;
        if (!IsStableId(Contribution.ContributionId) || !IsStableId(Contribution.Operation)
            || static_cast<uint8>(Contribution.Category)
                > static_cast<uint8>(EUnrealMCPExtensionCategory::ExistingAssetContributor)
            || static_cast<uint8>(Contribution.Access)
                > static_cast<uint8>(EUnrealMCPExtensionAccess::Mutation)
            || static_cast<uint8>(Contribution.Persistence)
                > static_cast<uint8>(EUnrealMCPExtensionPersistence::BlueprintCompileAndSave)
            || !ToolFamilies.Contains(Contribution.ToolFamily)
            || !IsStableId(Contribution.TargetFamily)
            || Contribution.TargetClassPath.IsEmpty() || Contribution.TargetClassPath.Len() > 512
            || (!Contribution.RequiredLiveCapability.IsEmpty()
                && !IsStableId(Contribution.RequiredLiveCapability))
            || (Contribution.Access == EUnrealMCPExtensionAccess::Read)
                != ReadToolFamilies.Contains(Contribution.ToolFamily)
            || (Contribution.Access == EUnrealMCPExtensionAccess::Read
                && Contribution.Persistence != EUnrealMCPExtensionPersistence::None)
            || Contribution.AllowedArgumentFields.Num() > 32
            || Contribution.StableLimits.Num() > 32
            || !Contribution.Handler.IsValid()
            || LocalIds.Contains(Contribution.ContributionId) || LocalOperations.Contains(OperationKey))
        {
            return Reject(TEXT("invalid_contribution"));
        }
        if (LoadObject<UClass>(nullptr, *Contribution.TargetClassPath) == nullptr)
            return Reject(TEXT("target_class_unavailable"));
        for (const FString& Field : Contribution.AllowedArgumentFields)
        {
            if (!IsStableId(Field)) return Reject(TEXT("invalid_contribution"));
        }
        for (const TPair<FString, int32>& Limit : Contribution.StableLimits)
        {
            if (!IsStableId(Limit.Key) || Limit.Value < 1)
                return Reject(TEXT("invalid_contribution"));
        }
        FString UnavailableReason;
        if (!Contribution.Handler->IsReady(UnavailableReason))
            return Reject(TEXT("live_capability_unavailable"));
        LocalIds.Add(Contribution.ContributionId);
        LocalOperations.Add(OperationKey);
        for (const FAcceptedRecord& Existing : Accepted)
        {
            for (const FUnrealMCPExtensionContribution& ExistingContribution : Existing.Registration.Contributions)
            {
                if ((ExistingContribution.ToolFamily + TEXT("|") + ExistingContribution.Operation) == OperationKey
                    || (ExistingContribution.Category == Contribution.Category
                        && ExistingContribution.TargetFamily == Contribution.TargetFamily))
                {
                    return Reject(TEXT("contribution_collision"));
                }
            }
        }
    }
    for (const FString& RequiredModule : Registration.RequiredEngineModules)
    {
        if (!IsNativeName(RequiredModule)
            || !FModuleManager::Get().IsModuleLoaded(FName(*RequiredModule)))
        {
            return Reject(TEXT("engine_module_unloaded"));
        }
    }

    FAcceptedRecord AcceptedRecord;
    AcceptedRecord.Handle.Value = NextHandle++;
    AcceptedRecord.Registration = Registration;
    AcceptedRecord.Registration.Contributions.Sort(
        [](const FUnrealMCPExtensionContribution& Left,
           const FUnrealMCPExtensionContribution& Right)
        {
            return Left.ContributionId < Right.ContributionId;
        });
    AcceptedRecord.OwningModule = &OwningModule;
    Result.bAccepted = true;
    Result.Handle = AcceptedRecord.Handle;
    Accepted.Add(MoveTemp(AcceptedRecord));
    Accepted.Sort([](const FAcceptedRecord& Left, const FAcceptedRecord& Right)
    {
        return Left.Registration.ExtensionId < Right.Registration.ExtensionId;
    });
    Descriptor->bRegistered = true;
    return Result;
}

void FUnrealMCPExtensionRegistry::Unregister(
    FUnrealMCPRegistrationHandle Handle,
    IModuleInterface& OwningModule)
{
    if (!IsInGameThread() || !Handle.IsValid())
    {
        return;
    }
    const int32 Index = Accepted.IndexOfByPredicate([&](const FAcceptedRecord& Value)
    {
        return Value.Handle == Handle && Value.OwningModule == &OwningModule;
    });
    if (Index == INDEX_NONE)
    {
        return;
    }
    const FString ExtensionId = Accepted[Index].Registration.ExtensionId;
    Accepted.RemoveAt(Index);
    if (FDescriptorRecord* Descriptor = Descriptors.FindByPredicate([&](const FDescriptorRecord& Value)
        { return Value.ExtensionId == ExtensionId; }))
    {
        Descriptor->bRegistered = false;
        Descriptor->UnavailableReason = bShuttingDown
            ? TEXT("shutting_down") : TEXT("restart_required");
    }
}

void FUnrealMCPExtensionRegistry::Freeze()
{
    bFrozen = true;
}

void FUnrealMCPExtensionRegistry::BeginShutdown()
{
    bShuttingDown = true;
}

bool FUnrealMCPExtensionRegistry::HasExtensionRequest(
    const TSharedPtr<FJsonObject>& Arguments) const
{
    return Arguments.IsValid() && Arguments->HasTypedField<EJson::String>(TEXT("extension_id"));
}

const FUnrealMCPExtensionContribution* FUnrealMCPExtensionRegistry::FindContribution(
    const FString& ExtensionId,
    const FString& ToolFamily,
    const FString& Operation,
    const FAcceptedRecord*& OutOwner) const
{
    OutOwner = nullptr;
    for (const FAcceptedRecord& Record : Accepted)
    {
        if (Record.Registration.ExtensionId != ExtensionId) continue;
        for (const FUnrealMCPExtensionContribution& Contribution : Record.Registration.Contributions)
        {
            if (Contribution.ToolFamily == ToolFamily && Contribution.Operation == Operation)
            {
                OutOwner = &Record;
                return &Contribution;
            }
        }
    }
    return nullptr;
}

const FUnrealMCPExtensionContribution* FUnrealMCPExtensionRegistry::FindBlueprintFamilyContribution(
    const UClass* Class,
    const FAcceptedRecord*& OutOwner) const
{
    OutOwner = nullptr;
    if (!bFrozen || bShuttingDown || Class == nullptr)
    {
        return nullptr;
    }
    for (const FAcceptedRecord& Record : Accepted)
    {
        for (const FUnrealMCPExtensionContribution& Contribution : Record.Registration.Contributions)
        {
            if (Contribution.Category != EUnrealMCPExtensionCategory::AssetFamily
                || Contribution.Access != EUnrealMCPExtensionAccess::Read
                || Contribution.ToolFamily != TEXT("blueprint_inspect"))
            {
                continue;
            }
            FString UnavailableReason;
            UClass* TargetClass = LoadObject<UClass>(nullptr, *Contribution.TargetClassPath);
            const bool bClassMatches = TargetClass != nullptr
                && (Contribution.bAllowDerivedTargetClasses
                    ? Class->IsChildOf(TargetClass) : Class == TargetClass);
            if (bClassMatches && Contribution.Handler->IsReady(UnavailableReason))
            {
                OutOwner = &Record;
                return &Contribution;
            }
        }
    }
    return nullptr;
}

bool FUnrealMCPExtensionRegistry::ClassifyBlueprintClass(
    const UClass* Class,
    FString& OutFamily,
    FString& OutNativeBaseClass) const
{
    const FAcceptedRecord* Owner = nullptr;
    const FUnrealMCPExtensionContribution* Contribution =
        FindBlueprintFamilyContribution(Class, Owner);
    if (Contribution == nullptr || Owner == nullptr)
    {
        return false;
    }
    OutFamily = Contribution->TargetFamily;
    OutNativeBaseClass = Contribution->TargetClassPath;
    return true;
}

bool FUnrealMCPExtensionRegistry::AppendBlueprintInspection(
    const UBlueprint& Blueprint,
    const TSharedPtr<FJsonObject>& Arguments,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    TArray<FString>& OutFingerprint,
    TSharedPtr<FJsonObject>& InOutFamilyCapabilities,
    FUnrealMCPError& OutError) const
{
    const UClass* BlueprintClass = Blueprint.GeneratedClass != nullptr
        ? Blueprint.GeneratedClass : Blueprint.ParentClass;
    const FAcceptedRecord* Owner = nullptr;
    const FUnrealMCPExtensionContribution* Contribution =
        FindBlueprintFamilyContribution(BlueprintClass, Owner);
    if (Contribution == nullptr || Owner == nullptr)
    {
        return true;
    }
    const UObject* Target = BlueprintClass != nullptr
        ? BlueprintClass->GetDefaultObject(false) : nullptr;
    if (Target == nullptr || !Contribution->Handler->SupportsTarget(*Target))
    {
        OutError = {TEXT("invalid_asset"),
            TEXT("The Blueprint generated class does not satisfy its companion family policy")};
        return false;
    }
    FUnrealMCPExtensionError ExtensionError;
    if (!Contribution->Handler->ValidateArguments(
        Contribution->Operation, Arguments, ExtensionError))
    {
        ConvertError(ExtensionError, OutError);
        return false;
    }
    const FString Before = SnapshotFor(
        *Target, Contribution->Operation, *Contribution->Handler, ExtensionError);
    if (Before.IsEmpty())
    {
        ConvertError(ExtensionError, OutError);
        return false;
    }
    TSharedPtr<FJsonObject> ExtensionResult;
    if (!Contribution->Handler->Inspect(
        *Target, Contribution->Operation, Arguments, ExtensionResult, ExtensionError))
    {
        ConvertError(ExtensionError, OutError);
        return false;
    }
    const FString After = SnapshotFor(
        *Target, Contribution->Operation, *Contribution->Handler, ExtensionError);
    if (After.IsEmpty())
    {
        ConvertError(ExtensionError, OutError);
        return false;
    }
    if (After != Before)
    {
        OutError = {TEXT("extension_contract_violation"),
            TEXT("The companion changed its target during Blueprint inspection")};
        return false;
    }
    if (!ExtensionResult.IsValid())
    {
        OutError = {TEXT("extension_contract_violation"),
            TEXT("The companion returned no Blueprint inspection result")};
        return false;
    }
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    const int32* RecordLimit = Contribution->StableLimits.Find(TEXT("records"));
    if (!ExtensionResult->TryGetArrayField(TEXT("records"), Records) || Records == nullptr
        || Records->Num() > (RecordLimit != nullptr ? *RecordLimit : 32)
        || OutRecords.Num() + Records->Num() > UnrealMCP::MaxInspectRecords)
    {
        OutError = {TEXT("response_too_large"),
            TEXT("The companion Blueprint inspection records exceed their stable bound")};
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Records)
    {
        const TSharedPtr<FJsonObject>* RecordObject = nullptr;
        if (!Value.IsValid() || !Value->TryGetObject(RecordObject)
            || RecordObject == nullptr || !RecordObject->IsValid())
        {
            OutError = {TEXT("extension_contract_violation"),
                TEXT("The companion returned a malformed Blueprint inspection record")};
            return false;
        }
        OutRecords.Add(Value);
    }
    const TSharedPtr<FJsonObject>* Capabilities = nullptr;
    if (ExtensionResult->TryGetObjectField(TEXT("family_capabilities"), Capabilities)
        && Capabilities != nullptr && Capabilities->IsValid())
    {
        if (!InOutFamilyCapabilities.IsValid())
        {
            InOutFamilyCapabilities = MakeShared<FJsonObject>();
        }
        InOutFamilyCapabilities->SetObjectField(
            Contribution->TargetFamily, Capabilities->ToSharedRef());
    }
    OutFingerprint.Add(Owner->Registration.ExtensionId + TEXT("|")
        + Contribution->ContributionId + TEXT("|") + Before);
    return true;
}

TArray<TSharedPtr<FJsonValue>> FUnrealMCPExtensionRegistry::BuildBlueprintFamilyCapabilities() const
{
    TArray<TSharedPtr<FJsonValue>> Result;
    TSet<FString> AddedFamilies;
    if (!bFrozen || bShuttingDown)
    {
        return Result;
    }
    for (const FAcceptedRecord& Record : Accepted)
    {
        for (const FUnrealMCPExtensionContribution& Contribution : Record.Registration.Contributions)
        {
            FString UnavailableReason;
            if (Contribution.Category != EUnrealMCPExtensionCategory::AssetFamily
                || Contribution.Access != EUnrealMCPExtensionAccess::Read
                || Contribution.ToolFamily != TEXT("blueprint_inspect")
                || AddedFamilies.Contains(Contribution.TargetFamily)
                || !Contribution.Handler->IsReady(UnavailableReason))
            {
                continue;
            }
            const TSharedRef<FJsonObject> Operations = MakeShared<FJsonObject>();
            for (const TCHAR* Name : {TEXT("discover"), TEXT("inspect")})
            {
                Operations->SetBoolField(Name, true);
            }
            for (const TCHAR* Name : {TEXT("create"), TEXT("compile"), TEXT("save"),
                TEXT("class_defaults"), TEXT("components"), TEXT("widget_tree"),
                TEXT("member_variables"), TEXT("functions"), TEXT("local_variables"),
                TEXT("macros"), TEXT("custom_events"), TEXT("action_catalog"),
                TEXT("graph_edit"), TEXT("parent_change"), TEXT("project_settings_assignment")})
            {
                Operations->SetBoolField(Name, false);
            }
            const TSharedRef<FJsonObject> Family = MakeShared<FJsonObject>();
            Family->SetStringField(TEXT("family"), Contribution.TargetFamily);
            Family->SetStringField(TEXT("native_base_class"), Contribution.TargetClassPath);
            Family->SetStringField(TEXT("inheritance_category"), TEXT("uobject_derived"));
            Family->SetStringField(TEXT("extension_id"), Record.Registration.ExtensionId);
            Family->SetObjectField(TEXT("operations"), Operations);
            const TSharedRef<FJsonObject> Multiplayer = MakeShared<FJsonObject>();
            Multiplayer->SetBoolField(TEXT("actor_replication"), false);
            Multiplayer->SetBoolField(TEXT("component_replication"), false);
            Multiplayer->SetBoolField(TEXT("replicated_variables"), false);
            Multiplayer->SetArrayField(TEXT("rpc_modes"), {
                MakeShared<FJsonValueString>(TEXT("not_replicated"))});
            Family->SetObjectField(TEXT("multiplayer"), Multiplayer);
            Result.Add(MakeShared<FJsonValueObject>(Family));
            AddedFamilies.Add(Contribution.TargetFamily);
        }
    }
    return Result;
}

bool FUnrealMCPExtensionRegistry::HasReadyFamilyCapability(
    const FString& TargetFamily,
    EUnrealMCPExtensionAccess Access) const
{
    if (!bFrozen || bShuttingDown)
    {
        return false;
    }
    for (const FAcceptedRecord& Record : Accepted)
    {
        for (const FUnrealMCPExtensionContribution& Contribution : Record.Registration.Contributions)
        {
            FString UnavailableReason;
            if (Contribution.TargetFamily == TargetFamily && Contribution.Access == Access
                && Contribution.Handler->IsReady(UnavailableReason))
            {
                return true;
            }
        }
    }
    return false;
}

bool FUnrealMCPExtensionRegistry::HasOnlyAllowedFields(
    const FJsonObject& Arguments,
    const FUnrealMCPExtensionContribution& Contribution)
{
    TSet<FString> Allowed(Contribution.AllowedArgumentFields);
    Allowed.Append({TEXT("extension_id"), TEXT("extension_schema_revision"), TEXT("operation"),
        TEXT("asset_path"), TEXT("operation_id"), TEXT("expected_snapshot")});
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments.Values)
    {
        if (!Allowed.Contains(Pair.Key)) return false;
    }
    return true;
}

bool FUnrealMCPExtensionRegistry::NormalizeAssetPath(
    const FString& Input,
    FString& OutObjectPath,
    FString& OutPackageName)
{
    if (!Input.StartsWith(TEXT("/")) || Input.StartsWith(TEXT("//"))
        || Input.Contains(TEXT("..")) || Input.Contains(TEXT("\\")) || Input.Len() > 512)
    {
        return false;
    }
    OutPackageName = FPackageName::ObjectPathToPackageName(Input);
    if (!FPackageName::IsValidLongPackageName(OutPackageName, true)) return false;
    OutObjectPath = Input.Contains(TEXT("."))
        ? Input
        : OutPackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(OutPackageName);
    return FPackageName::IsValidObjectPath(OutObjectPath)
        && FPackageName::ObjectPathToObjectName(OutObjectPath)
            == FPackageName::GetLongPackageAssetName(OutPackageName);
}

bool FUnrealMCPExtensionRegistry::ValidateMutationScope(
    const FString& PackageName,
    FUnrealMCPError& OutError)
{
    FString PhysicalTarget;
    if (!FPackageName::TryConvertLongPackageNameToFilename(PackageName, PhysicalTarget))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The target mount is unavailable")};
        return false;
    }
    PhysicalTarget = FPaths::ConvertRelativePathToFull(FPaths::GetPath(PhysicalTarget));
    FPaths::NormalizeDirectoryName(PhysicalTarget);
    if (PackageName.StartsWith(TEXT("/Game/")))
    {
        FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        FPaths::NormalizeDirectoryName(Root);
        if (!(FPaths::IsSamePath(PhysicalTarget, Root) || FPaths::IsUnderDirectory(PhysicalTarget, Root))
            || PathContainsSymlink(Root, PhysicalTarget))
        {
            OutError = {TEXT("mutation_scope_denied"), TEXT("Project content resolves outside its physical mount")};
            return false;
        }
        return true;
    }
    const int32 Slash = PackageName.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 1);
    FString MountDirectory;
    if (Slash == INDEX_NONE
        || !FPackageName::TryConvertLongPackageNameToFilename(PackageName.Left(Slash + 1), MountDirectory))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The target is outside mutable project content")};
        return false;
    }
    FString PluginRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir());
    FString PhysicalMount = FPaths::ConvertRelativePathToFull(MountDirectory);
    FPaths::NormalizeDirectoryName(PluginRoot);
    FPaths::NormalizeDirectoryName(PhysicalMount);
    if (!FPaths::IsUnderDirectory(PhysicalMount, PluginRoot)
        || !(FPaths::IsSamePath(PhysicalTarget, PhysicalMount) || FPaths::IsUnderDirectory(PhysicalTarget, PhysicalMount))
        || PathContainsSymlink(PluginRoot, PhysicalTarget))
    {
        OutError = {TEXT("mutation_scope_denied"), TEXT("The target mount is not owned by a local project plugin")};
        return false;
    }
    return true;
}

FString FUnrealMCPExtensionRegistry::SnapshotFor(
    const UObject& Target,
    const FString& Operation,
    const IUnrealMCPExtensionHandler& Handler,
    FUnrealMCPExtensionError& OutError)
{
    FString ContributionFingerprint;
    if (!Handler.AppendFingerprint(Target, Operation, ContributionFingerprint, OutError)
        || ContributionFingerprint.Len() > 16384)
    {
        return FString();
    }
    const FString Material = Target.GetPathName() + TEXT("|") + Target.GetClass()->GetPathName()
        + TEXT("|") + ContributionFingerprint;
    const FTCHARToUTF8 Utf8(*Material);
    uint8 Hash[FSHA1::DigestSize];
    FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Hash);
    return BytesToHex(Hash, UE_ARRAY_COUNT(Hash)).ToLower();
}

void FUnrealMCPExtensionRegistry::ConvertError(
    const FUnrealMCPExtensionError& Input,
    FUnrealMCPError& Output)
{
    Output.Code = Input.Code.IsEmpty() ? TEXT("internal_error") : Input.Code.Left(64);
    Output.Message = Input.Message.IsEmpty() ? TEXT("Companion operation failed") : Input.Message.Left(512);
    Output.Details = Input.Details.IsValid() ? Input.Details : MakeShared<FJsonObject>();
    Output.bRetryable = Input.bRetryable;
}

bool FUnrealMCPExtensionRegistry::Execute(
    const FString& ToolFamily,
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError) const
{
    check(IsInGameThread());
    FString ExtensionId;
    FString Operation;
    FString AssetPath;
    double SchemaNumber = 0.0;
    if (!bFrozen || bShuttingDown || !Arguments.IsValid()
        || !Arguments->TryGetStringField(TEXT("extension_id"), ExtensionId)
        || !Arguments->TryGetStringField(TEXT("operation"), Operation)
        || !Arguments->TryGetStringField(TEXT("asset_path"), AssetPath)
        || !Arguments->TryGetNumberField(TEXT("extension_schema_revision"), SchemaNumber)
        || !FMath::IsNearlyEqual(SchemaNumber, FMath::RoundToDouble(SchemaNumber))
        || static_cast<int32>(SchemaNumber) != UnrealMCP::ExtensionSchemaRevision)
    {
        OutError = {TEXT("extension_unavailable"), TEXT("The extension request is unavailable or malformed")};
        return false;
    }
    const FAcceptedRecord* Owner = nullptr;
    const FUnrealMCPExtensionContribution* Contribution = FindContribution(
        ExtensionId, ToolFamily, Operation, Owner);
    if (Contribution == nullptr || Owner == nullptr)
    {
        OutError = {TEXT("extension_unavailable"), TEXT("The requested extension operation is not registered")};
        return false;
    }
    if (!HasOnlyAllowedFields(*Arguments, *Contribution))
    {
        OutError = {TEXT("invalid_argument"), TEXT("The extension request contains an unsupported field")};
        return false;
    }
    FString UnavailableReason;
    if (!Contribution->Handler->IsReady(UnavailableReason))
    {
        OutError = {TEXT("extension_unavailable"), TEXT("The extension capability is no longer ready")};
        return false;
    }
    FUnrealMCPExtensionError ExtensionError;
    if (!Contribution->Handler->ValidateArguments(Operation, Arguments, ExtensionError))
    {
        ConvertError(ExtensionError, OutError);
        return false;
    }

    FString ObjectPath;
    FString PackageName;
    if (!NormalizeAssetPath(AssetPath, ObjectPath, PackageName))
    {
        OutError = {TEXT("invalid_path"), TEXT("asset_path must identify one exact mounted asset")};
        return false;
    }
    UClass* TargetClass = LoadObject<UClass>(nullptr, *Contribution->TargetClassPath);
    UObject* Target = LoadObject<UObject>(nullptr, *ObjectPath);
    if (TargetClass == nullptr || Target == nullptr
        || (Contribution->bAllowDerivedTargetClasses
            ? !Target->IsA(TargetClass) : Target->GetClass() != TargetClass)
        || !Contribution->Handler->SupportsTarget(*Target))
    {
        OutError = {TEXT("invalid_asset"), TEXT("The exact target does not satisfy the registered class policy")};
        return false;
    }
    const FString BeforeSnapshot = SnapshotFor(*Target, Operation, *Contribution->Handler, ExtensionError);
    if (BeforeSnapshot.IsEmpty())
    {
        ConvertError(ExtensionError, OutError);
        return false;
    }
    if (Contribution->Access == EUnrealMCPExtensionAccess::Read)
    {
        if (!Contribution->Handler->Inspect(*Target, Operation, Arguments, OutResult, ExtensionError))
        {
            ConvertError(ExtensionError, OutError);
            return false;
        }
        const FString AfterInspection = SnapshotFor(
            *Target, Operation, *Contribution->Handler, ExtensionError);
        if (AfterInspection != BeforeSnapshot)
        {
            OutError = {TEXT("extension_contract_violation"),
                TEXT("The companion changed its target during a read operation")};
            return false;
        }
        if (!OutResult.IsValid()) OutResult = MakeShared<FJsonObject>();
        OutResult->SetStringField(TEXT("extension_id"), ExtensionId);
        OutResult->SetNumberField(TEXT("extension_schema_revision"), Owner->Registration.ExtensionSchemaRevision);
        OutResult->SetStringField(TEXT("snapshot"), BeforeSnapshot);
        return true;
    }

    FString ExpectedSnapshot;
    if (!Arguments->TryGetStringField(TEXT("expected_snapshot"), ExpectedSnapshot)
        || ExpectedSnapshot != BeforeSnapshot)
    {
        OutError = {TEXT("stale_precondition"), TEXT("The extension target snapshot is stale")};
        return false;
    }
    if (!ValidateMutationScope(PackageName, OutError)) return false;
    if (Contribution->Persistence != EUnrealMCPExtensionPersistence::None
        && Target->GetOutermost()->IsDirty())
    {
        OutError = {TEXT("busy"),
            TEXT("The extension target package has unrelated unsaved changes")};
        return false;
    }
    if (GEditor == nullptr)
    {
        OutError = {TEXT("editor_unavailable"), TEXT("The editor transaction subsystem is unavailable"), MakeShared<FJsonObject>(), true};
        return false;
    }
    TUniquePtr<FScopedTransaction> Transaction = MakeUnique<FScopedTransaction>(
        FText::FromString(TEXT("Unreal MCP companion operation")));
    Target->Modify();
    TSharedPtr<FJsonObject> Change;
    if (!Contribution->Handler->ApplyMutation(*Target, Operation, Arguments, Change, ExtensionError))
    {
        Transaction->Cancel();
        Transaction.Reset();
        ConvertError(ExtensionError, OutError);
        return false;
    }
    UBlueprint* BlueprintToCompile = nullptr;
    if (Contribution->Persistence == EUnrealMCPExtensionPersistence::BlueprintCompileAndSave)
    {
        BlueprintToCompile = Cast<UBlueprint>(Target);
        if (BlueprintToCompile == nullptr)
        {
            Transaction->Cancel();
            Transaction.Reset();
            OutError = {TEXT("extension_contract_violation"),
                TEXT("Blueprint compile persistence requires a Blueprint target")};
            return false;
        }
        FKismetEditorUtilities::CompileBlueprint(BlueprintToCompile);
    }
    TSharedPtr<FJsonObject> ReadBack;
    const FString AfterSnapshot = SnapshotFor(*Target, Operation, *Contribution->Handler, ExtensionError);
    const bool bCompileSucceeded = BlueprintToCompile == nullptr
        || BlueprintToCompile->Status != BS_Error;
    const bool bReadBack = bCompileSucceeded && !AfterSnapshot.IsEmpty()
        && Contribution->Handler->ReadBack(*Target, Operation, Arguments, ReadBack, ExtensionError);
    if (!bReadBack)
    {
        Transaction.Reset();
        const bool bUndone = GEditor->UndoTransaction(false);
        if (BlueprintToCompile != nullptr) FKismetEditorUtilities::CompileBlueprint(BlueprintToCompile);
        FUnrealMCPExtensionError RollbackError;
        const FString RestoredSnapshot = SnapshotFor(*Target, Operation, *Contribution->Handler, RollbackError);
        OutError = {TEXT("rollback_failed"),
            bUndone && RestoredSnapshot == BeforeSnapshot
                ? TEXT("The extension mutation failed postcondition verification and was rolled back")
                : TEXT("The extension mutation failed and exact rollback could not be verified")};
        return false;
    }
    const bool bChangedContent = AfterSnapshot != BeforeSnapshot;
    if (!bChangedContent)
    {
        Transaction->Cancel();
    }
    Transaction.Reset();
    if (bChangedContent && Contribution->Persistence != EUnrealMCPExtensionPersistence::None)
    {
        const TArray<UPackage*> Packages = {Target->GetOutermost()};
        if (!UEditorLoadingAndSavingUtils::SavePackages(Packages, true)
            || Target->GetOutermost()->IsDirty())
        {
            const bool bUndone = GEditor->UndoTransaction(false);
            if (BlueprintToCompile != nullptr)
                FKismetEditorUtilities::CompileBlueprint(BlueprintToCompile);
            FUnrealMCPExtensionError RestoreError;
            const FString RestoredSnapshot = SnapshotFor(
                *Target, Operation, *Contribution->Handler, RestoreError);
            const bool bRestored = bUndone && RestoredSnapshot == BeforeSnapshot
                && UEditorLoadingAndSavingUtils::SavePackages(Packages, true)
                && !Target->GetOutermost()->IsDirty();
            OutError = {bRestored ? TEXT("save_failed") : TEXT("rollback_failed"),
                bRestored
                    ? TEXT("The extension mutation could not be saved and was rolled back")
                    : TEXT("The extension save failed and exact rollback could not be verified")};
            return false;
        }
    }
    OutResult = ReadBack.IsValid() ? ReadBack : MakeShared<FJsonObject>();
    OutResult->SetStringField(TEXT("extension_id"), ExtensionId);
    OutResult->SetNumberField(TEXT("extension_schema_revision"), Owner->Registration.ExtensionSchemaRevision);
    OutResult->SetStringField(TEXT("previous_snapshot"), BeforeSnapshot);
    OutResult->SetStringField(TEXT("snapshot"), AfterSnapshot);
    OutResult->SetBoolField(TEXT("changed_content"), bChangedContent);
    OutResult->SetBoolField(TEXT("package_dirty"), Target->GetOutermost()->IsDirty());
    if (Change.IsValid()) OutResult->SetObjectField(TEXT("changed"), Change);
    return true;
}

FString FUnrealMCPExtensionRegistry::RegistrySignature() const
{
    FString Material;
    for (const FAcceptedRecord& Record : Accepted)
    {
        Material += Record.Registration.ExtensionId + TEXT("|")
            + FString::FromInt(Record.Registration.ExtensionSchemaRevision) + TEXT("|")
            + FString::FromInt(Record.Registration.CompanionApiVersion) + TEXT(";");
        for (const FUnrealMCPExtensionContribution& Contribution : Record.Registration.Contributions)
        {
            Material += Contribution.ToolFamily + TEXT(":") + Contribution.Operation + TEXT(":")
                + AccessName(Contribution.Access) + TEXT(";");
        }
    }
    const FTCHARToUTF8 Utf8(*Material);
    uint8 Hash[FSHA1::DigestSize];
    FSHA1::HashBuffer(Utf8.Get(), Utf8.Length(), Hash);
    return BytesToHex(Hash, UE_ARRAY_COUNT(Hash)).ToLower();
}

TSharedPtr<FJsonObject> FUnrealMCPExtensionRegistry::BuildCapabilities() const
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetNumberField(TEXT("companion_api_version"), UnrealMCP::CompanionApiVersion);
    Result->SetNumberField(TEXT("extension_schema_revision"), UnrealMCP::ExtensionSchemaRevision);
    Result->SetStringField(TEXT("registry_signature"), RegistrySignature());
    TArray<TSharedPtr<FJsonValue>> CompanionValues;
    for (const FDescriptorRecord& Descriptor : Descriptors)
    {
        const FAcceptedRecord* AcceptedRecord = Accepted.FindByPredicate([&](const FAcceptedRecord& Value)
        {
            return Value.Registration.ExtensionId == Descriptor.ExtensionId;
        });
        bool bHandlersReady = AcceptedRecord != nullptr;
        if (AcceptedRecord != nullptr)
        {
            for (const FUnrealMCPExtensionContribution& Contribution : AcceptedRecord->Registration.Contributions)
            {
                FString HandlerReason;
                if (!Contribution.Handler->IsReady(HandlerReason))
                {
                    bHandlersReady = false;
                    break;
                }
            }
        }
        const bool bReady = AcceptedRecord != nullptr && bHandlersReady && !bShuttingDown;
        const TSharedRef<FJsonObject> Companion = MakeShared<FJsonObject>();
        Companion->SetStringField(TEXT("plugin_name"), Descriptor.PluginName);
        Companion->SetStringField(TEXT("extension_id"), Descriptor.ExtensionId);
        Companion->SetStringField(TEXT("semantic_version"), Descriptor.SemanticVersion);
        Companion->SetNumberField(TEXT("companion_api_version"), Descriptor.CompanionApiVersion);
        Companion->SetNumberField(TEXT("schema_revision"), Descriptor.SchemaRevision);
        Companion->SetBoolField(TEXT("ready"), bReady);
        bool bReadSupport = false;
        bool bMutationSupport = false;
        Companion->SetStringField(TEXT("unavailable_reason"), bReady
            ? TEXT("")
            : (bShuttingDown ? TEXT("shutting_down")
                : (!bHandlersReady && AcceptedRecord != nullptr
                    ? TEXT("live_capability_unavailable")
                    : Descriptor.UnavailableReason.Left(128))));
        Companion->SetArrayField(TEXT("required_engine_plugins"), Strings(Descriptor.RequiredEnginePlugins));
        TArray<TSharedPtr<FJsonValue>> Contributions;
        if (AcceptedRecord != nullptr)
        {
            for (const FUnrealMCPExtensionContribution& Contribution : AcceptedRecord->Registration.Contributions)
            {
                const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
                Value->SetStringField(TEXT("contribution_id"), Contribution.ContributionId);
                Value->SetStringField(TEXT("category"), CategoryName(Contribution.Category));
                Value->SetStringField(TEXT("access"), AccessName(Contribution.Access));
                Value->SetStringField(TEXT("persistence"), PersistenceName(Contribution.Persistence));
                Value->SetStringField(TEXT("tool_family"), Contribution.ToolFamily);
                Value->SetStringField(TEXT("operation"), Contribution.Operation);
                Value->SetStringField(TEXT("target_family"), Contribution.TargetFamily);
                Value->SetStringField(TEXT("required_live_capability"), Contribution.RequiredLiveCapability);
                const TSharedRef<FJsonObject> Limits = MakeShared<FJsonObject>();
                for (const TPair<FString, int32>& Limit : Contribution.StableLimits)
                {
                    Limits->SetNumberField(Limit.Key, Limit.Value);
                }
                Value->SetObjectField(TEXT("limits"), Limits);
                bReadSupport |= Contribution.Access == EUnrealMCPExtensionAccess::Read;
                bMutationSupport |= Contribution.Access == EUnrealMCPExtensionAccess::Mutation;
                Contributions.Add(MakeShared<FJsonValueObject>(Value));
            }
        }
        Companion->SetArrayField(TEXT("contributions"), Contributions);
        Companion->SetBoolField(TEXT("read_support"), bReadSupport);
        Companion->SetBoolField(TEXT("mutation_support"), bMutationSupport);
        CompanionValues.Add(MakeShared<FJsonValueObject>(Companion));
    }
    Result->SetArrayField(TEXT("companions"), CompanionValues);
    Result->SetArrayField(TEXT("registration_diagnostics"), Strings(Diagnostics));
    return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
void FUnrealMCPExtensionRegistry::AddDescriptorForTesting(
    const FUnrealMCPCompanionRegistration& Registration,
    bool bEnabled,
    const FString& UnavailableReason)
{
    FDescriptorRecord Record;
    Record.PluginName = Registration.PluginName;
    Record.ExtensionId = Registration.ExtensionId;
    Record.OwningModule = Registration.OwningModule;
    Record.SemanticVersion = Registration.SemanticVersion;
    Record.CompanionApiVersion = Registration.CompanionApiVersion;
    Record.SchemaRevision = Registration.ExtensionSchemaRevision;
    Record.RequiredEnginePlugins = Registration.RequiredEnginePlugins;
    Record.bEnabled = bEnabled;
    Record.UnavailableReason = UnavailableReason;
    Descriptors.Add(MoveTemp(Record));
    Descriptors.Sort([](const FDescriptorRecord& Left, const FDescriptorRecord& Right)
    {
        return Left.ExtensionId < Right.ExtensionId;
    });
}
#endif
