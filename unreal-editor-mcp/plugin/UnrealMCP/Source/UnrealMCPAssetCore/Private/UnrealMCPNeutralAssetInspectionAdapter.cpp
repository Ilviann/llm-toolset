#include "UnrealMCPNeutralAssetInspectionAdapter.h"

#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPAssetInspectionService.h"
#include "UnrealMCPVersion.h"
#include "UObject/Object.h"

namespace UnrealMCP::AssetCore::Private
{
bool IsMediaClass(const FString& ClassPath)
{
    static const TArray<FString> Markers = {
        TEXT("Texture"), TEXT("StaticMesh"), TEXT("SkeletalMesh"), TEXT("SoundWave"),
        TEXT("MediaSource"), TEXT("MediaTexture"), TEXT("AnimSequence"), TEXT("AnimMontage"),
        TEXT("FontFace"), TEXT("GeometryCache")};
    for (const FString& Marker : Markers)
    {
        if (ClassPath.Contains(Marker)) return true;
    }
    return false;
}

class FNeutralAssetInspectionAdapter final : public IUnrealMCPAssetFamilyInspectionAdapter
{
public:
    bool Inspect(
        const FUnrealMCPAssetFamilyInspectionContext& Context,
        FUnrealMCPAssetFamilyDocumentBuilder& Document,
        FUnrealMCPAssetFamilySelectorRouter& Selectors,
        FUnrealMCPAssetFamilySnapshotBuilder& Snapshot,
        FUnrealMCPError& OutError) override
    {
        if (Context.Asset == nullptr)
        {
            OutError = {TEXT("invalid_argument"), TEXT("The neutral asset inspection adapter requires a resolved asset")};
            return false;
        }
        if (!Context.Selector.IsRoot())
        {
            OutError = {TEXT("unsupported_type"), TEXT("This asset family supports identity inspection only")};
            return false;
        }
        if (Context.bHasPaging)
        {
            OutError = {TEXT("invalid_argument"), TEXT("Paging parameters require a pageable collection selector")};
            return false;
        }
        if (Context.bHasPartialGraphFlag)
        {
            OutError = {TEXT("invalid_argument"), TEXT("allow_partial_graph applies only to graph selectors")};
            return false;
        }

        const FString StorageClass = Context.Asset->GetClass()->GetPathName();
        const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
        Result->SetStringField(TEXT("format"), TEXT("yaml"));
        Result->SetNumberField(TEXT("schema_version"), 1);
        Result->SetStringField(TEXT("snapshot_id"), Context.Identity.SnapshotId);
        const TSharedRef<FUnrealMCPRecord> Asset = MakeShared<FUnrealMCPRecord>();
        Asset->SetStringField(TEXT("path"), Context.Identity.ObjectPath);
        Asset->SetStringField(TEXT("type"), TEXT("asset"));
        Asset->SetStringField(TEXT("storage_class"), StorageClass);
        Result->SetObjectField(TEXT("asset"), Asset);
        TArray<TSharedPtr<FUnrealMCPValue>> Limitations;
        Limitations.Add(MakeShared<FUnrealMCPValueString>(
            IsMediaClass(StorageClass) ? TEXT("media_type_only") : TEXT("unsupported_family")));
        Limitations.Add(MakeShared<FUnrealMCPValueString>(TEXT("no_media_or_bulk_data")));
        Result->SetArrayField(TEXT("limitations"), MoveTemp(Limitations));

        for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Field : Result->Values)
        {
            if (!Document.Add({Field.Key, TEXT("value"), Field.Value}, OutError)) return false;
        }
        return Snapshot.Add(TEXT("released_snapshot"), Context.Identity.SnapshotId, OutError)
            && Selectors.Freeze(OutError);
    }
};
}

bool UnrealMCP::AssetCore::RegisterNeutralAssetAdapter(
    FUnrealMCPAssetFamilyRegistry& Registry,
    FUnrealMCPError& OutError)
{
    FUnrealMCPAssetFamilyDescriptor Descriptor;
    Descriptor.FamilyId = TEXT("neutral_asset");
    Descriptor.NativeClass = UObject::StaticClass();
    Descriptor.ClassPolicy = EUnrealMCPAssetFamilyClassPolicy::ExactAndDerived;
    Descriptor.Priority = 0;
    Descriptor.RequiredModules.Add(TEXT("UnrealMCPAssetCore"));
    Descriptor.Bounds.MaxDocumentBytes = 4 * 1024 * 1024;
    Descriptor.Bounds.MaxValueNodes = 65536;
    Descriptor.Limits = {
        {TEXT("page_size"), UnrealMCP::MaxAssetInspectPageSize},
        {TEXT("selector_bytes"), UnrealMCP::MaxAssetInspectSelectorBytes},
        {TEXT("complete_graph_bytes"), UnrealMCP::MaxAssetInspectCompleteGraphBytes}};
    Descriptor.Capabilities.bInspection = true;
    Descriptor.InspectionAdapter = MakeShared<Private::FNeutralAssetInspectionAdapter>();
    return Registry.Register(MoveTemp(Descriptor), OutError);
}
