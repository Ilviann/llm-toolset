#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "UnrealMCPWireTypes.h"

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
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError* Error = nullptr;
    TSharedPtr<FUnrealMCPError> OwnedError;
};

class UNREALMCPASSETCORE_API FUnrealMCPOperationLedger
{
public:
    FUnrealMCPOperationLedger(FString InBridgeInstanceId, FString InContextBinding, TFunction<double()> InNow = [] { return FPlatformTime::Seconds(); });

    FUnrealMCPOperationAdmission Admit(const FString& Command, const TSharedPtr<FUnrealMCPRecord>& Arguments);
    bool MarkExecuting(const FString& OperationId, FUnrealMCPError& OutError);
    void Commit(const FString& OperationId, const TSharedPtr<FUnrealMCPRecord>& Result);
    void Complete(
        const FString& OperationId,
        const FString& State,
        const TSharedPtr<FUnrealMCPRecord>& Result);
    void Reject(const FString& OperationId, const FUnrealMCPError& Error);
    bool Status(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    bool Cancel(const TSharedPtr<FUnrealMCPRecord>& Arguments, TSharedPtr<FUnrealMCPRecord>& OutResult, FUnrealMCPError& OutError);
    void CancelQueued();
    TSharedPtr<FUnrealMCPRecord> CurrentState() const;

    const FString& GetBridgeInstanceId() const { return BridgeInstanceId; }
    static FString DigestRequest(const FString& Command, const TSharedPtr<FUnrealMCPRecord>& Arguments, const FString& ContextBinding);

private:
    struct FEntry
    {
        FString Command;
        FString Digest;
        FString State;
        double CreatedAt = 0.0;
        double ExpiresAt = 0.0;
        TSharedPtr<FUnrealMCPRecord> Result;
        TSharedPtr<FUnrealMCPError> Error;
    };

    void RemoveExpiredLocked(double CurrentTime);
    bool MakeRoomLocked();
    TSharedRef<FUnrealMCPRecord> EntryStatusLocked(const FString& OperationId, const FEntry& Entry) const;

    FString BridgeInstanceId;
    FString ContextBinding;
    TFunction<double()> Now;
    mutable FCriticalSection Mutex;
    TMap<FString, FEntry> Entries;
};
