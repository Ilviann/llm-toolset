#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

class FJsonObject;
struct FUnrealMCPError;

enum class EUnrealMCPOperationAdmission : uint8
{
    Accepted,
    ReplaySuccess,
    ReplayError,
    Conflict,
    Busy,
};

struct FUnrealMCPOperationAdmission
{
    EUnrealMCPOperationAdmission Kind = EUnrealMCPOperationAdmission::Busy;
    FString OperationId;
    FString RequestDigest;
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError* Error = nullptr;
    TSharedPtr<FUnrealMCPError> OwnedError;
};

class FUnrealMCPOperationLedger
{
public:
    FUnrealMCPOperationLedger(FString InBridgeInstanceId, FString InContextBinding, TFunction<double()> InNow = [] { return FPlatformTime::Seconds(); });

    FUnrealMCPOperationAdmission Admit(const FString& Command, const TSharedPtr<FJsonObject>& Arguments);
    bool MarkExecuting(const FString& OperationId, FUnrealMCPError& OutError);
    void Commit(const FString& OperationId, const TSharedPtr<FJsonObject>& Result);
    void Reject(const FString& OperationId, const FUnrealMCPError& Error);
    bool Status(const TSharedPtr<FJsonObject>& Arguments, TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError);
    void CancelQueued();
    TSharedPtr<FJsonObject> CurrentState() const;

    const FString& GetBridgeInstanceId() const { return BridgeInstanceId; }
    static FString DigestRequest(const FString& Command, const TSharedPtr<FJsonObject>& Arguments, const FString& ContextBinding);

private:
    struct FEntry
    {
        FString Command;
        FString Digest;
        FString State;
        double CreatedAt = 0.0;
        double ExpiresAt = 0.0;
        TSharedPtr<FJsonObject> Result;
        TSharedPtr<FUnrealMCPError> Error;
    };

    void RemoveExpiredLocked(double CurrentTime);
    bool MakeRoomLocked();
    TSharedRef<FJsonObject> EntryStatusLocked(const FString& OperationId, const FEntry& Entry) const;

    FString BridgeInstanceId;
    FString ContextBinding;
    TFunction<double()> Now;
    mutable FCriticalSection Mutex;
    TMap<FString, FEntry> Entries;
};
