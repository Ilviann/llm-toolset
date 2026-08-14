#include "UnrealMCPCommandCatalog.h"

#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPExtensionRegistry.h"
#include "UnrealMCPOperationLedger.h"
#include "UnrealMCPVersion.h"

namespace
{
FUnrealMCPNativeFeature FixedFeature(const TCHAR* Name, bool bValue = true)
{
    return {Name, [bValue]() { return bValue; }};
}
FUnrealMCPNativeLimit FixedLimit(const TCHAR* Name, double Value)
{
    return {Name, Value};
}
}

bool FUnrealMCPCommandCatalogBuilder::Register(
    FUnrealMCPCommandDescriptor Descriptor,
    EUnrealMCPCommandSource Source,
    bool bProvidesModelSchema,
    FString& OutError)
{
    if (bFrozen)
    {
        OutError = TEXT("Native command catalog is frozen");
        return false;
    }
    if (Source != EUnrealMCPCommandSource::FixedNative || bProvidesModelSchema)
    {
        OutError = TEXT("Runtime-provided commands and schemas are forbidden");
        return false;
    }
    if (Descriptor.Identity.IsEmpty() || Descriptor.Handler == nullptr)
    {
        OutError = TEXT("Native command descriptors require an identity and handler");
        return false;
    }
    if (CommandIndexes.Contains(Descriptor.Identity))
    {
        OutError = FString::Printf(TEXT("Duplicate native command: %s"), *Descriptor.Identity);
        return false;
    }
    for (const FUnrealMCPNativeFeature& Feature : Descriptor.Features)
    {
        if (Feature.Name.IsEmpty() || Feature.Resolve == nullptr || FeatureNames.Contains(Feature.Name))
        {
            OutError = FString::Printf(TEXT("Conflicting native capability: %s"), *Feature.Name);
            return false;
        }
    }
    for (const FUnrealMCPNativeLimit& Limit : Descriptor.Limits)
    {
        if (Limit.Name.IsEmpty() || !FMath::IsFinite(Limit.Value) || LimitNames.Contains(Limit.Name))
        {
            OutError = FString::Printf(TEXT("Conflicting native limit: %s"), *Limit.Name);
            return false;
        }
    }
    const int32 Index = Descriptors.Add(MoveTemp(Descriptor));
    CommandIndexes.Add(Descriptors[Index].Identity, Index);
    for (const FUnrealMCPNativeFeature& Feature : Descriptors[Index].Features) FeatureNames.Add(Feature.Name);
    for (const FUnrealMCPNativeLimit& Limit : Descriptors[Index].Limits) LimitNames.Add(Limit.Name);
    return true;
}

bool FUnrealMCPCommandCatalogBuilder::Freeze(FString& OutError)
{
    if (bFrozen)
    {
        OutError = TEXT("Native command catalog is already frozen");
        return false;
    }
    FeatureNames.Reset();
    LimitNames.Reset();
    TSet<int32> ExplicitOrders;
    for (const FUnrealMCPCommandDescriptor& Descriptor : Descriptors)
    {
        if (Descriptor.Order != INDEX_NONE
            && (Descriptor.Order < 0 || ExplicitOrders.Contains(Descriptor.Order)))
        {
            OutError = FString::Printf(TEXT("Conflicting native command order: %d"), Descriptor.Order);
            return false;
        }
        if (Descriptor.Order != INDEX_NONE) ExplicitOrders.Add(Descriptor.Order);
        for (const FUnrealMCPNativeFeature& Feature : Descriptor.Features)
        {
            if (Feature.Name.IsEmpty() || Feature.Resolve == nullptr || FeatureNames.Contains(Feature.Name))
            {
                OutError = FString::Printf(TEXT("Conflicting native capability: %s"), *Feature.Name);
                return false;
            }
            FeatureNames.Add(Feature.Name);
        }
        for (const FUnrealMCPNativeLimit& Limit : Descriptor.Limits)
        {
            if (Limit.Name.IsEmpty() || !FMath::IsFinite(Limit.Value) || LimitNames.Contains(Limit.Name))
            {
                OutError = FString::Printf(TEXT("Conflicting native limit: %s"), *Limit.Name);
                return false;
            }
            LimitNames.Add(Limit.Name);
        }
    }
    Descriptors.Sort([](const FUnrealMCPCommandDescriptor& Left, const FUnrealMCPCommandDescriptor& Right)
    {
        if (Left.Order != Right.Order)
        {
            if (Left.Order == INDEX_NONE) return false;
            if (Right.Order == INDEX_NONE) return true;
            return Left.Order < Right.Order;
        }
        return Left.Identity < Right.Identity;
    });
    CommandIndexes.Reset();
    for (int32 Index = 0; Index < Descriptors.Num(); ++Index)
    {
        CommandIndexes.Add(Descriptors[Index].Identity, Index);
    }
    bFrozen = true;
    return true;
}

const FUnrealMCPCommandDescriptor* FUnrealMCPCommandCatalogBuilder::Find(const FString& Command) const
{
    const int32* Index = CommandIndexes.Find(Command);
    return Index != nullptr ? &Descriptors[*Index] : nullptr;
}

FUnrealMCPCommandDescriptor* FUnrealMCPCommandCatalogBuilder::FindMutable(const FString& Command)
{
    if (bFrozen) return nullptr;
    const int32* Index = CommandIndexes.Find(Command);
    return Index != nullptr ? &Descriptors[*Index] : nullptr;
}

TArray<TSharedPtr<FUnrealMCPValue>> FUnrealMCPCommandCatalogBuilder::BuildCommandNames() const
{
    TArray<TSharedPtr<FUnrealMCPValue>> Names;
    Names.Reserve(Descriptors.Num());
    for (const FUnrealMCPCommandDescriptor& Descriptor : Descriptors)
    {
        Names.Add(MakeShared<FUnrealMCPValueString>(Descriptor.Identity));
    }
    return Names;
}

TSharedRef<FUnrealMCPRecord> FUnrealMCPCommandCatalogBuilder::BuildFeatures() const
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    for (const FUnrealMCPCommandDescriptor& Descriptor : Descriptors)
    {
        for (const FUnrealMCPNativeFeature& Feature : Descriptor.Features)
        {
            Result->SetBoolField(Feature.Name, Feature.Resolve());
        }
    }
    return Result;
}

TSharedRef<FUnrealMCPRecord> FUnrealMCPCommandCatalogBuilder::BuildLimits() const
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    for (const FUnrealMCPCommandDescriptor& Descriptor : Descriptors)
    {
        for (const FUnrealMCPNativeLimit& Limit : Descriptor.Limits)
        {
            Result->SetNumberField(Limit.Name, Limit.Value);
        }
    }
    return Result;
}

FUnrealMCPCommandCatalog::FUnrealMCPCommandCatalog(
    FString InProjectHash,
    FString InBridgeInstanceId,
    FUnrealMCPOperationLedger& InOperationLedger,
    TSharedRef<FUnrealMCPAssetFamilyRegistry> InAssetFamilyRegistry,
    TSharedRef<FUnrealMCPExtensionRegistry> InExtensionRegistry,
    TArray<IUnrealMCPBuiltInDomainModule*> InDomainModules,
    FUnrealMCPCommandHostHandlers InHostHandlers)
    : ProjectHash(MoveTemp(InProjectHash)), BridgeInstanceId(MoveTemp(InBridgeInstanceId)),
      OperationLedger(InOperationLedger), AssetFamilyRegistry(MoveTemp(InAssetFamilyRegistry)),
      ExtensionRegistry(MoveTemp(InExtensionRegistry))
{
    if (!AssetFamilyRegistry->IsFrozen())
    {
        InitializationError = TEXT("Built-in asset-family registry must be frozen before command composition");
        return;
    }
    Build(MoveTemp(InDomainModules), MoveTemp(InHostHandlers));
}

FUnrealMCPCommandCatalog::~FUnrealMCPCommandCatalog() = default;

bool FUnrealMCPCommandCatalog::Register(FUnrealMCPCommandDescriptor Descriptor)
{
    if (!InitializationError.IsEmpty()) return false;
    return Catalog.Register(MoveTemp(Descriptor), EUnrealMCPCommandSource::FixedNative, false, InitializationError);
}

void FUnrealMCPCommandCatalog::Build(
    TArray<IUnrealMCPBuiltInDomainModule*> DomainModules,
    FUnrealMCPCommandHostHandlers HostHandlers)
{
    auto Add = [this](
        const TCHAR* Identity,
        int32 Order,
        EUnrealMCPCommandAccess Access,
        EUnrealMCPRetainedOperationPolicy Retained,
        EUnrealMCPCommandDispatch Dispatch,
        FUnrealMCPCommandHandler Handler)
    {
        FUnrealMCPCommandDescriptor Descriptor;
        Descriptor.Identity = Identity;
        Descriptor.Order = Order;
        Descriptor.Access = Access;
        Descriptor.RetainedOperation = Retained;
        Descriptor.Dispatch = Dispatch;
        Descriptor.Handler = MoveTemp(Handler);
        Register(MoveTemp(Descriptor));
    };
    using Access = EUnrealMCPCommandAccess;
    using Retained = EUnrealMCPRetainedOperationPolicy;
    using Dispatch = EUnrealMCPCommandDispatch;

    Add(TEXT("capabilities"), 0, Access::ReadOnly, Retained::None, Dispatch::GameThread, MoveTemp(HostHandlers.Capabilities));
    Add(TEXT("editor_state"), 1, Access::ReadOnly, Retained::None, Dispatch::GameThread, MoveTemp(HostHandlers.EditorState));
    Add(TEXT("editor_shutdown"), 2, Access::Internal, Retained::None, Dispatch::GameThread, MoveTemp(HostHandlers.EditorShutdown));
    Add(TEXT("operation_status"), 3, Access::ReadOnly, Retained::None, Dispatch::RequestThread,
        [this](const auto& Arguments, auto& Result, auto& Error) { return OperationLedger.Status(Arguments, Result, Error); });
    Add(TEXT("operation_cancel"), 4, Access::Writable, Retained::None, Dispatch::RequestThread,
        [this](const auto& Arguments, auto& Result, auto& Error) { return OperationLedger.Cancel(Arguments, Result, Error); });

    FUnrealMCPCommandDescriptor* Capabilities = Catalog.FindMutable(TEXT("capabilities"));
    if (Capabilities == nullptr)
    {
        InitializationError = TEXT("Native command catalog is missing capabilities");
        return;
    }
    for (const TCHAR* Feature : {
        TEXT("editor_lifecycle"), TEXT("graceful_editor_shutdown"),
        TEXT("companion_plugins")})
    {
        Capabilities->Features.Add(FixedFeature(Feature));
    }
    Capabilities->Features.Add(FixedFeature(TEXT("project_build"), false));
    auto ExtensionFeature = [this, Capabilities](const TCHAR* Name, const TCHAR* Family, EUnrealMCPExtensionAccess Access)
    {
        Capabilities->Features.Add({Name, [this, Family = FString(Family), Access]()
        {
            return ExtensionRegistry->HasReadyFamilyCapability(Family, Access);
        }});
    };
    ExtensionFeature(TEXT("gas_ability_blueprints_inspection"), TEXT("gameplay_ability"), EUnrealMCPExtensionAccess::Read);
    ExtensionFeature(TEXT("gas_ability_blueprints_mutation"), TEXT("gameplay_ability"), EUnrealMCPExtensionAccess::Mutation);
    ExtensionFeature(TEXT("gas_gameplay_effects_inspection"), TEXT("gameplay_effect"), EUnrealMCPExtensionAccess::Read);
    ExtensionFeature(TEXT("gas_gameplay_effects_mutation"), TEXT("gameplay_effect"), EUnrealMCPExtensionAccess::Mutation);
    ExtensionFeature(TEXT("commonui_widget_blueprints_inspection"), TEXT("commonui_widget"), EUnrealMCPExtensionAccess::Read);
    ExtensionFeature(TEXT("commonui_widget_blueprints_mutation"), TEXT("commonui_widget"), EUnrealMCPExtensionAccess::Mutation);

    Capabilities->Limits = {
        FixedLimit(TEXT("request_bytes"), UnrealMCP::MaxRequestBytes),
        FixedLimit(TEXT("companion_descriptors"), UnrealMCP::MaxDiscoveredCompanions), FixedLimit(TEXT("companions"), UnrealMCP::MaxAcceptedCompanions),
        FixedLimit(TEXT("companion_contributions"), UnrealMCP::MaxCompanionContributions), FixedLimit(TEXT("companion_capability_records"), UnrealMCP::MaxCompanionCapabilityRecords),
        FixedLimit(TEXT("companion_diagnostics"), UnrealMCP::MaxCompanionDiagnostics), FixedLimit(TEXT("extension_id_chars"), UnrealMCP::MaxExtensionIdChars),
        FixedLimit(TEXT("response_bytes"), UnrealMCP::MaxResponseBytes), FixedLimit(TEXT("queued_requests"), UnrealMCP::MaxQueuedRequests),
        FixedLimit(TEXT("json_depth"), UnrealMCP::MaxJsonDepth), FixedLimit(TEXT("string_chars"), UnrealMCP::MaxStringLength),
        FixedLimit(TEXT("command_deadline_ms"), UnrealMCP::CommandDeadlineSeconds * 1000.0), FixedLimit(TEXT("inspect_page_size"), UnrealMCP::MaxInspectPageSize),
        FixedLimit(TEXT("discovery_scan"), UnrealMCP::MaxDiscoveryScan),
        FixedLimit(TEXT("inspect_records"), UnrealMCP::MaxInspectRecords), FixedLimit(TEXT("retained_cursors"), UnrealMCP::MaxRetainedCursors),
        FixedLimit(TEXT("cursor_lifetime_ms"), UnrealMCP::CursorLifetimeSeconds * 1000.0),
        FixedLimit(TEXT("retained_operations"), UnrealMCP::MaxRetainedOperations),
        FixedLimit(TEXT("operation_lifetime_ms"), UnrealMCP::OperationLifetimeSeconds * 1000.0)};
    const FUnrealMCPDomainRegistrar Registrar(
        [this](FUnrealMCPCommandDescriptor Descriptor, FString& OutError)
        {
            if (Catalog.Register(MoveTemp(Descriptor), EUnrealMCPCommandSource::FixedNative, false, OutError))
            {
                return true;
            }
            return false;
        },
        [this](FUnrealMCPNativeFeature Feature, FString& OutError)
        {
            if (Feature.Name.IsEmpty() || Feature.Resolve == nullptr)
            {
                OutError = TEXT("Native domain capability requires an identity and resolver");
                return false;
            }
            FUnrealMCPCommandDescriptor* Capabilities = Catalog.FindMutable(TEXT("capabilities"));
            if (Capabilities == nullptr)
            {
                OutError = TEXT("Native command catalog is missing capabilities");
                return false;
            }
            for (const FUnrealMCPNativeFeature& Existing : Capabilities->Features)
            {
                if (Existing.Name == Feature.Name) return true;
            }
            Capabilities->Features.Add(MoveTemp(Feature));
            return true;
        },
        [this](FUnrealMCPNativeLimit Limit, FString& OutError)
        {
            if (Limit.Name.IsEmpty() || !FMath::IsFinite(Limit.Value))
            {
                OutError = TEXT("Native domain limit requires a finite named value");
                return false;
            }
            FUnrealMCPCommandDescriptor* Capabilities = Catalog.FindMutable(TEXT("capabilities"));
            if (Capabilities == nullptr)
            {
                OutError = TEXT("Native command catalog is missing capabilities");
                return false;
            }
            for (const FUnrealMCPNativeLimit& Existing : Capabilities->Limits)
            {
                if (Existing.Name == Limit.Name)
                {
                    if (FMath::IsNearlyEqual(Existing.Value, Limit.Value)) return true;
                    OutError = FString::Printf(TEXT("Conflicting native limit: %s"), *Limit.Name);
                    return false;
                }
            }
            Capabilities->Limits.Add(MoveTemp(Limit));
            return true;
        },
        [this](FUnrealMCPBlueprintFamilyProvider Provider, FString& OutError)
        {
            if (Provider == nullptr || BlueprintFamilyProvider != nullptr)
            {
                OutError = TEXT("Native Blueprint-family provider is missing or duplicated");
                return false;
            }
            BlueprintFamilyProvider = MoveTemp(Provider);
            return true;
        });

    FUnrealMCPDomainContext Context{
        ProjectHash,
        BridgeInstanceId,
        AssetFamilyRegistry,
        *ExtensionRegistry,
        [this](const TCHAR* Message, FUnrealMCPError& Error)
        {
            return RejectConcurrentRetainedOperation(Message, Error);
        }};

    TSet<FName> DomainNames;
    for (IUnrealMCPBuiltInDomainModule* DomainModule : DomainModules)
    {
        if (DomainModule == nullptr || DomainModule->GetDomainName().IsNone()
            || DomainNames.Contains(DomainModule->GetDomainName()))
        {
            InitializationError = TEXT("Built-in domain modules must be non-null and uniquely named");
            return;
        }
        DomainNames.Add(DomainModule->GetDomainName());
        if (!DomainModule->RegisterCommands(Registrar, Context, InitializationError))
        {
            return;
        }
    }
    if (BlueprintFamilyProvider == nullptr)
    {
        InitializationError = TEXT("Built-in Blueprint domain did not register its family provider");
        return;
    }
    Catalog.Freeze(InitializationError);
}
bool FUnrealMCPCommandCatalog::Execute(const FString& Command, const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    const FUnrealMCPCommandDescriptor* Descriptor = Catalog.Find(Command);
    if (Descriptor == nullptr)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Unknown or unavailable command")};
        return false;
    }
    if (Descriptor->bAllowsExtensionRequests && ExtensionRegistry->HasExtensionRequest(Arguments))
    {
        return ExtensionRegistry->Execute(Command, Arguments, OutResult, OutError);
    }
    return Descriptor->Handler(Arguments, OutResult, OutError);
}

TArray<TSharedPtr<FUnrealMCPValue>> FUnrealMCPCommandCatalog::BuildBlueprintFamilyCapabilities() const
{
    TArray<TSharedPtr<FUnrealMCPValue>> Result = BlueprintFamilyProvider();
    Result.Append(ExtensionRegistry->BuildBlueprintFamilyCapabilities());
    return Result;
}

bool FUnrealMCPCommandCatalog::RejectConcurrentRetainedOperation(const TCHAR* Message, FUnrealMCPError& OutError) const
{
    const TSharedPtr<FUnrealMCPRecord> State = OperationLedger.CurrentState();
    if (static_cast<int32>(State->GetNumberField(TEXT("queued"))) > 0
        || static_cast<int32>(State->GetNumberField(TEXT("executing"))) > 1)
    {
        OutError = {TEXT("busy"), Message, MakeShared<FUnrealMCPRecord>(), true};
        return true;
    }
    return false;
}
