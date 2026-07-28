#include "UnrealMCPAssetReferenceLiveScanner.h"

#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UnrealMCPAssetReferenceTargetResolver.h"
#include "UnrealMCPVersion.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace UnrealMCPAssetReferenceLiveScannerPrivate
{
struct FScanCounts
{
    bool bSupported = true;
    bool bTruncated = false;
    int32 Candidates = 0;
    int32 Scanned = 0;
    int32 Records = 0;
};

class FDirectTargetReferenceFinder final : public FReferenceFinder
{
public:
    FDirectTargetReferenceFinder(TArray<UObject*>& InScratch, UObject* InTarget)
        : FReferenceFinder(InScratch, nullptr, false, false, false, false)
        , Target(InTarget)
    {
    }

    void Reset()
    {
        ReferenceCount = 0;
        Properties.Reset();
    }

    int32 GetReferenceCount() const
    {
        return ReferenceCount;
    }

    const TArray<FProperty*>& GetProperties() const
    {
        return Properties;
    }

    virtual void HandleObjectReference(
        UObject*& Object,
        const UObject* ReferencingObject,
        const FProperty* ReferencingProperty) override
    {
        if (Object != Target)
        {
            return;
        }
        ++ReferenceCount;
        if (ReferencingProperty != nullptr)
        {
            Properties.AddUnique(const_cast<FProperty*>(ReferencingProperty));
        }
    }

private:
    UObject* Target = nullptr;
    int32 ReferenceCount = 0;
    TArray<FProperty*> Properties;
};

TSharedRef<FJsonObject> ScanStatus(const FScanCounts& Counts)
{
    const bool bComplete = Counts.bSupported && !Counts.bTruncated;
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(
        TEXT("status"),
        !Counts.bSupported ? TEXT("unsupported")
        : Counts.bTruncated ? TEXT("truncated")
        : TEXT("complete"));
    Result->SetBoolField(TEXT("complete"), bComplete);
    Result->SetBoolField(TEXT("truncated"), Counts.bTruncated);
    Result->SetBoolField(TEXT("unsupported"), !Counts.bSupported);
    Result->SetBoolField(TEXT("stale"), false);
    Result->SetNumberField(TEXT("candidate_count"), Counts.Candidates);
    Result->SetNumberField(TEXT("scanned_count"), Counts.Scanned);
    Result->SetNumberField(TEXT("record_count"), Counts.Records);
    return Result;
}

FString SafeObjectPath(const UObject* Object)
{
    return Object != nullptr ? Object->GetPathName().Left(512) : FString();
}

FString SafeClassPath(const UObject* Object)
{
    return Object != nullptr && Object->GetClass() != nullptr
        ? Object->GetClass()->GetClassPathName().ToString().Left(512)
        : FString();
}
}

void FUnrealMCPAssetReferenceLiveScanner::Scan(
    UObject* TargetObject,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    TSharedPtr<FJsonObject>& OutStatus,
    int32& OutOpenEditorCount) const
{
    using namespace UnrealMCPAssetReferenceLiveScannerPrivate;
    FScanCounts Counts;
    OutOpenEditorCount = 0;
    if (TargetObject == nullptr)
    {
        Counts.bSupported = false;
        OutStatus = ScanStatus(Counts);
        return;
    }

    UAssetEditorSubsystem* AssetEditors =
        GEditor != nullptr
        ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()
        : nullptr;
    if (AssetEditors != nullptr)
    {
        const TArray<IAssetEditorInstance*> Editors =
            AssetEditors->FindEditorsForAsset(TargetObject);
        OutOpenEditorCount = Editors.Num();
        for (IAssetEditorInstance* Editor : Editors)
        {
            ++Counts.Candidates;
            if (OutRecords.Num() >= UnrealMCP::MaxAssetReferenceRecords)
            {
                Counts.bTruncated = true;
                break;
            }
            const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("section"), TEXT("live_memory"));
            Record->SetStringField(TEXT("evidence"), TEXT("live_memory"));
            Record->SetStringField(TEXT("live_kind"), TEXT("asset_editor"));
            Record->SetStringField(
                TEXT("edited_asset_path"),
                SafeObjectPath(TargetObject));
            Record->SetStringField(
                TEXT("editor_name"),
                Editor != nullptr
                    ? Editor->GetEditorName().ToString().Left(128)
                    : FString());
            OutRecords.Add(MakeShared<FJsonValueObject>(Record));
            ++Counts.Records;
        }
    }

    TArray<UObject*> ScratchReferences;
    FDirectTargetReferenceFinder Finder(ScratchReferences, TargetObject);
    int32 Visited = 0;
    for (FThreadSafeObjectIterator It; It; ++It)
    {
        UObject* Potential = *It;
        if (Potential == nullptr || Potential == TargetObject
            || Potential->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed))
        {
            continue;
        }
        ++Counts.Candidates;
        if (Visited >= UnrealMCP::MaxAssetReferenceLiveObjects)
        {
            Counts.bTruncated = true;
            break;
        }
        ++Visited;
        ++Counts.Scanned;
        Finder.Reset();
        Finder.FindReferences(Potential);
        const int32 ReferenceCount = Finder.GetReferenceCount();
        if (ReferenceCount <= 0)
        {
            continue;
        }
        if (OutRecords.Num() >= UnrealMCP::MaxAssetReferenceRecords)
        {
            Counts.bTruncated = true;
            continue;
        }
        const FString PackageName =
            Potential->GetOutermost() != nullptr
            ? Potential->GetOutermost()->GetName().Left(512)
            : FString();
        const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("section"), TEXT("live_memory"));
        Record->SetStringField(TEXT("evidence"), TEXT("live_memory"));
        Record->SetStringField(
            TEXT("live_kind"),
            Potential->GetWorld() != nullptr
                ? TEXT("world_object")
                : TEXT("loaded_object"));
        Record->SetStringField(
            TEXT("referencer_object_path"),
            SafeObjectPath(Potential));
        Record->SetStringField(
            TEXT("referencer_object_class"),
            SafeClassPath(Potential));
        Record->SetStringField(TEXT("referencer_package"), PackageName);
        Record->SetStringField(
            TEXT("mount_point"),
            FUnrealMCPAssetReferenceTargetResolver::PackageMount(PackageName));
        Record->SetNumberField(
            TEXT("reference_count"),
            FMath::Min(ReferenceCount, 65535));
        TArray<FProperty*> Properties = Finder.GetProperties();
        Properties.Sort([](const FProperty& Left, const FProperty& Right)
        {
            return Left.GetName() < Right.GetName();
        });
        TArray<TSharedPtr<FJsonValue>> PropertyValues;
        FString LastName;
        for (FProperty* Property : Properties)
        {
            const FString Name =
                Property != nullptr ? Property->GetName().Left(128) : FString();
            if (Name.IsEmpty() || Name == LastName)
            {
                continue;
            }
            if (PropertyValues.Num() >= UnrealMCP::MaxAssetReferenceProperties)
            {
                Record->SetBoolField(TEXT("properties_truncated"), true);
                break;
            }
            LastName = Name;
            PropertyValues.Add(MakeShared<FJsonValueString>(Name));
        }
        Record->SetArrayField(TEXT("properties"), PropertyValues);
        if (!Record->HasField(TEXT("properties_truncated")))
        {
            Record->SetBoolField(TEXT("properties_truncated"), false);
        }
        OutRecords.Add(MakeShared<FJsonValueObject>(Record));
        ++Counts.Records;
    }
    OutStatus = ScanStatus(Counts);
}
