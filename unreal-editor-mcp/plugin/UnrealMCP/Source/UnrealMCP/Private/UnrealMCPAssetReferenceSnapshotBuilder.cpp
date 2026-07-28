#include "UnrealMCPAssetReferenceSnapshotBuilder.h"

#include "Misc/SecureHash.h"

namespace UnrealMCPAssetReferenceSnapshotBuilderPrivate
{
FString HashReferenceText(const FString& Text)
{
    FTCHARToUTF8 Encoded(*Text);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

FString RecordFingerprint(const TSharedPtr<FJsonValue>& Value)
{
    const TSharedPtr<FJsonObject> Record =
        Value.IsValid() ? Value->AsObject() : nullptr;
    if (!Record.IsValid())
    {
        return FString();
    }
    TArray<FString> Parts;
    for (const TCHAR* Field : {
        TEXT("section"), TEXT("evidence"), TEXT("dependency_category"),
        TEXT("target_identifier"), TEXT("target_granularity"),
        TEXT("referencer_identifier"), TEXT("referencer_package"),
        TEXT("mount_point"), TEXT("referencer_primary_asset_id"),
        TEXT("referencer_asset_path"), TEXT("referencer_asset_class"),
        TEXT("live_kind"), TEXT("edited_asset_path"), TEXT("editor_name"),
        TEXT("referencer_object_path"), TEXT("referencer_object_class")})
    {
        FString Part;
        Record->TryGetStringField(Field, Part);
        Parts.Add(FString(Field) + TEXT("=") + Part);
    }
    for (const TCHAR* Field : {
        TEXT("dependency_property_bits"), TEXT("reference_count")})
    {
        double Part = 0.0;
        Record->TryGetNumberField(Field, Part);
        Parts.Add(
            FString(Field) + TEXT("=")
            + LexToString(static_cast<int64>(Part)));
    }
    for (const TCHAR* Field : {
        TEXT("hard"), TEXT("direct"), TEXT("asset_class_known"),
        TEXT("properties_truncated")})
    {
        bool Part = false;
        Record->TryGetBoolField(Field, Part);
        Parts.Add(
            FString(Field) + TEXT("=")
            + (Part ? TEXT("true") : TEXT("false")));
    }
    for (const TCHAR* Field : {
        TEXT("dependency_properties"), TEXT("properties")})
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        FString ArrayText;
        if (Record->TryGetArrayField(Field, Values) && Values != nullptr)
        {
            for (const TSharedPtr<FJsonValue>& ArrayValue : *Values)
            {
                ArrayText +=
                    ArrayValue.IsValid()
                    ? ArrayValue->AsString() + TEXT(",")
                    : TEXT(",");
            }
        }
        Parts.Add(FString(Field) + TEXT("=") + ArrayText);
    }
    return FString::Join(Parts, TEXT("|"));
}
}

bool FUnrealMCPAssetReferenceSnapshotBuilder::Capture(
    const FString& AssetPath,
    uint64 SnapshotRegistrySerial,
    TFunctionRef<uint64()> CurrentRegistrySerial,
    FUnrealMCPAssetReferenceSnapshot& OutSnapshot,
    FUnrealMCPError& OutError) const
{
    using namespace UnrealMCPAssetReferenceSnapshotBuilderPrivate;
    check(IsInGameThread());
    FUnrealMCPResolvedAssetReferenceTarget Target;
    if (!TargetResolver.Resolve(AssetPath, Target, OutError))
    {
        return false;
    }

    OutSnapshot = FUnrealMCPAssetReferenceSnapshot();
    OutSnapshot.AssetPath = AssetPath;
    OutSnapshot.RegistrySerial = SnapshotRegistrySerial;
    OutSnapshot.Target = Target.Metadata;
    RegistryScanner.Scan(
        Target,
        SnapshotRegistrySerial,
        CurrentRegistrySerial,
        OutSnapshot.Records,
        OutSnapshot.Scans);
    TSharedPtr<FJsonObject> LiveStatus;
    int32 OpenEditorCount = 0;
    LiveScanner.Scan(
        Target.LoadedObject,
        OutSnapshot.Records,
        LiveStatus,
        OpenEditorCount);
    OutSnapshot.Scans->SetObjectField(
        TEXT("live_memory"),
        LiveStatus.ToSharedRef());
    OutSnapshot.Target->SetNumberField(
        TEXT("open_editor_count"),
        OpenEditorCount);

    OutSnapshot.Records.Sort(
        [](const TSharedPtr<FJsonValue>& Left,
           const TSharedPtr<FJsonValue>& Right)
        {
            return RecordFingerprint(Left) < RecordFingerprint(Right);
        });

    const FPrimaryAssetId PrimaryId = Target.Asset.GetPrimaryAssetId();
    TArray<FString> Fingerprint{
        AssetPath,
        Target.PackageName,
        Target.Asset.AssetName.ToString(),
        Target.Asset.AssetClassPath.ToString(),
        FUnrealMCPAssetReferenceTargetResolver::PackageMount(
            Target.PackageName),
        Target.LoadedObject != nullptr ? TEXT("loaded") : TEXT("unloaded"),
        Target.Asset.IsRedirector() ? TEXT("redirector") : TEXT("asset"),
        PrimaryId.IsValid() ? PrimaryId.ToString() : FString(),
        LexToString(OpenEditorCount)};
    for (const TSharedPtr<FJsonValue>& Record : OutSnapshot.Records)
    {
        Fingerprint.Add(RecordFingerprint(Record));
    }
    for (const TCHAR* ScanName : {
        TEXT("serialized"), TEXT("management"), TEXT("searchable_name"),
        TEXT("live_memory")})
    {
        const TSharedPtr<FJsonObject> Status =
            OutSnapshot.Scans->GetObjectField(ScanName);
        Fingerprint.Add(
            FString(ScanName) + TEXT("|")
            + Status->GetStringField(TEXT("status"))
            + TEXT("|")
            + LexToString(
                Status->GetIntegerField(TEXT("candidate_count")))
            + TEXT("|")
            + LexToString(
                Status->GetIntegerField(TEXT("scanned_count")))
            + TEXT("|")
            + LexToString(
                Status->GetIntegerField(TEXT("record_count"))));
    }
    OutSnapshot.SnapshotId =
        HashReferenceText(FString::Join(Fingerprint, TEXT("\n")));
    return true;
}
