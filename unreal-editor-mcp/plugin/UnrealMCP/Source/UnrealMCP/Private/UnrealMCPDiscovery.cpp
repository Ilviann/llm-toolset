#include "UnrealMCPDiscovery.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMCPVersion.h"

FUnrealMCPDiscovery::FUnrealMCPDiscovery(FString InStateDirectory, FString InProjectHash, uint32 InPort)
    : StateDirectory(MoveTemp(InStateDirectory)), ProjectHash(MoveTemp(InProjectHash)), Port(InPort)
{
}

bool FUnrealMCPDiscovery::Write(FString& OutError) const
{
    const FDateTime Now = FDateTime::UtcNow();
    const int64 Timestamp = Now.ToUnixTimestamp() * 1000LL + Now.GetMillisecond();
    const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
    Record->SetStringField(TEXT("project_hash"), ProjectHash);
    Record->SetNumberField(TEXT("process_id"), FPlatformProcess::GetCurrentProcessId());
    Record->SetNumberField(TEXT("port"), Port);
    Record->SetStringField(TEXT("bridge_version"), UnrealMCP::Version);
    Record->SetStringField(TEXT("unreal_version"), FEngineVersion::Current().ToString(EVersionComponent::Changelist));
    Record->SetNumberField(TEXT("updated_at_ms"), static_cast<double>(Timestamp));
    FString Json;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    if (!FJsonSerializer::Serialize(Record, Writer) || Json.Len() > 4096)
    {
        OutError = TEXT("could not serialize discovery heartbeat");
        return false;
    }
    const FString Target = Path();
    const FString Temporary = FString::Printf(TEXT("%s.tmp-%s"), *Target, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if (!FFileHelper::SaveStringToFile(Json, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        OutError = TEXT("could not persist discovery heartbeat");
        return false;
    }
    IFileManager& Files = IFileManager::Get();
    if (!Files.Move(*Target, *Temporary, true, true, false, true))
    {
        Files.Delete(*Temporary, false, true, true);
        OutError = TEXT("could not atomically install discovery heartbeat");
        return false;
    }
    FString Verified;
    if (!FFileHelper::LoadFileToString(Verified, *Target) || Verified != Json)
    {
        Files.Delete(*Target, false, true, true);
        OutError = TEXT("could not verify discovery heartbeat");
        return false;
    }
    return true;
}

void FUnrealMCPDiscovery::Remove() const
{
    IFileManager::Get().Delete(*Path(), false, true, true);
}

FString FUnrealMCPDiscovery::Path() const
{
    return FPaths::Combine(StateDirectory, TEXT("discovery.json"));
}
