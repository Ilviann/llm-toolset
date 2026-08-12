#include "UnrealMCPOperationLedger.h"

#include "UnrealMCPWireTypes.h"
#include "UnrealMCPWireTypes.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "UnrealMCPJsonCodec.h"
#include "UnrealMCPProtocol.h"
#include "UnrealMCPVersion.h"

namespace
{
bool IsOperationId(const FString& Value)
{
    if (Value.Len() != 32) return false;
    for (const TCHAR Character : Value)
    {
        if (!FChar::IsHexDigit(Character) || FChar::IsUpper(Character)) return false;
    }
    return true;
}

FString QuoteJsonString(const FString& Value)
{
    FString Output;
    UnrealMCP::JsonCodec::SerializeValue(MakeShared<FUnrealMCPValueString>(Value), Output);
    return Output;
}

FString CanonicalValue(const TSharedPtr<FUnrealMCPValue>& Value);

FString CanonicalObject(const TSharedPtr<FUnrealMCPRecord>& Object)
{
    if (!Object.IsValid()) return TEXT("null");
    TArray<TPair<FString, TSharedPtr<FUnrealMCPValue>>> Values;
    Values.Reserve(Object->Values.Num());
    for (const auto& Pair : Object->Values) Values.Emplace(FString(Pair.Key), Pair.Value);
    Values.Sort([](const auto& Left, const auto& Right) { return Left.Key < Right.Key; });
    TArray<FString> Fields;
    Fields.Reserve(Values.Num());
    for (const auto& Pair : Values)
    {
        Fields.Add(QuoteJsonString(Pair.Key) + TEXT(":") + CanonicalValue(Pair.Value));
    }
    return TEXT("{") + FString::Join(Fields, TEXT(",")) + TEXT("}");
}

FString CanonicalValue(const TSharedPtr<FUnrealMCPValue>& Value)
{
    if (!Value.IsValid()) return TEXT("null");
    switch (Value->Type)
    {
    case EUnrealMCPValueType::Null: return TEXT("null");
    case EUnrealMCPValueType::String: return QuoteJsonString(Value->AsString());
    case EUnrealMCPValueType::Boolean: return Value->AsBool() ? TEXT("true") : TEXT("false");
    case EUnrealMCPValueType::Number: return FString::Printf(TEXT("%.17g"), Value->AsNumber());
    case EUnrealMCPValueType::Record: return CanonicalObject(Value->AsObject());
    case EUnrealMCPValueType::Array:
        {
            TArray<FString> Items;
            for (const TSharedPtr<FUnrealMCPValue>& Item : Value->AsArray()) Items.Add(CanonicalValue(Item));
            return TEXT("[") + FString::Join(Items, TEXT(",")) + TEXT("]");
        }
    default: return TEXT("null");
    }
}

TSharedRef<FUnrealMCPRecord> ErrorValue(const FUnrealMCPError& Error)
{
    const TSharedRef<FUnrealMCPRecord> Value = MakeShared<FUnrealMCPRecord>();
    Value->SetStringField(TEXT("code"), Error.Code.Left(64));
    Value->SetStringField(TEXT("message"), Error.Message.Left(512));
    Value->SetObjectField(TEXT("details"), Error.Details.IsValid() ? Error.Details : MakeShared<FUnrealMCPRecord>());
    Value->SetBoolField(TEXT("retryable"), Error.bRetryable);
    return Value;
}

bool ParseOperationIdentity(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    const FString& Command,
    FString& OutOperationId,
    FString& OutBridgeInstanceId,
    FUnrealMCPError& OutError)
{
    if (!Arguments.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("arguments must be an object")};
        return false;
    }
    for (const TPair<FString, TSharedPtr<FUnrealMCPValue>>& Pair : Arguments->Values)
    {
        if (Pair.Key != TEXT("operation_id") && Pair.Key != TEXT("bridge_instance_id"))
        {
            OutError = {
                TEXT("invalid_argument"),
                Command + TEXT(" contains an unknown field")};
            return false;
        }
    }
    if (!Arguments->TryGetStringField(TEXT("operation_id"), OutOperationId)
        || !IsOperationId(OutOperationId)
        || !Arguments->TryGetStringField(TEXT("bridge_instance_id"), OutBridgeInstanceId)
        || !IsOperationId(OutBridgeInstanceId))
    {
        OutError = {
            TEXT("invalid_argument"),
            Command + TEXT(" requires valid operation_id and bridge_instance_id")};
        return false;
    }
    return true;
}

TSharedRef<FUnrealMCPRecord> UnknownOperationStatus(
    const FString& OperationId,
    const FString& BridgeInstanceId)
{
    const TSharedRef<FUnrealMCPRecord> Unknown = MakeShared<FUnrealMCPRecord>();
    Unknown->SetStringField(TEXT("operation_id"), OperationId);
    Unknown->SetStringField(TEXT("bridge_instance_id"), BridgeInstanceId);
    Unknown->SetStringField(TEXT("state"), TEXT("outcome_unknown"));
    Unknown->SetBoolField(TEXT("retained"), false);
    Unknown->SetBoolField(TEXT("retry_safe"), false);
    return Unknown;
}
}

FUnrealMCPOperationLedger::FUnrealMCPOperationLedger(FString InBridgeInstanceId, FString InContextBinding, TFunction<double()> InNow)
    : BridgeInstanceId(MoveTemp(InBridgeInstanceId)), ContextBinding(MoveTemp(InContextBinding)), Now(MoveTemp(InNow))
{
}

FString FUnrealMCPOperationLedger::DigestRequest(
    const FString& Command,
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    const FString& ContextBinding)
{
    const FString Canonical = Command + TEXT("\n") + CanonicalObject(Arguments) + TEXT("\n") + ContextBinding;
    FTCHARToUTF8 Encoded(*Canonical);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

void FUnrealMCPOperationLedger::RemoveExpiredLocked(double CurrentTime)
{
    for (auto It = Entries.CreateIterator(); It; ++It)
    {
        if (It.Value().ExpiresAt <= CurrentTime && It.Value().State != TEXT("queued") && It.Value().State != TEXT("executing"))
        {
            It.RemoveCurrent();
        }
    }
}

bool FUnrealMCPOperationLedger::MakeRoomLocked()
{
    if (Entries.Num() < UnrealMCP::MaxRetainedOperations) return true;
    FString OldestId;
    double Oldest = TNumericLimits<double>::Max();
    for (const TPair<FString, FEntry>& Pair : Entries)
    {
        if (Pair.Value.State != TEXT("queued") && Pair.Value.State != TEXT("executing") && Pair.Value.CreatedAt < Oldest)
        {
            Oldest = Pair.Value.CreatedAt;
            OldestId = Pair.Key;
        }
    }
    if (OldestId.IsEmpty()) return false;
    Entries.Remove(OldestId);
    return true;
}

FUnrealMCPOperationAdmission FUnrealMCPOperationLedger::Admit(const FString& Command, const TSharedPtr<FUnrealMCPRecord>& Arguments)
{
    FUnrealMCPOperationAdmission Admission;
    FString OperationId;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("operation_id"), OperationId) || !IsOperationId(OperationId))
    {
        Admission.Kind = EUnrealMCPOperationAdmission::Conflict;
        Admission.OwnedError = MakeShared<FUnrealMCPError>(FUnrealMCPError{TEXT("invalid_argument"), TEXT("Every retained operation requires one 32-character lowercase hexadecimal operation_id")});
        Admission.Error = Admission.OwnedError.Get();
        return Admission;
    }
    Admission.OperationId = OperationId;
    Admission.RequestDigest = DigestRequest(Command, Arguments, ContextBinding);
    FScopeLock Lock(&Mutex);
    const double CurrentTime = Now();
    RemoveExpiredLocked(CurrentTime);
    if (const FEntry* Existing = Entries.Find(OperationId))
    {
        if (Existing->Digest != Admission.RequestDigest)
        {
            Admission.Kind = EUnrealMCPOperationAdmission::Conflict;
            Admission.OwnedError = MakeShared<FUnrealMCPError>(FUnrealMCPError{TEXT("operation_conflict"), TEXT("operation_id is already bound to different normalized arguments")});
            Admission.Error = Admission.OwnedError.Get();
        }
        else if (Existing->State == TEXT("committed")
            || Existing->State == TEXT("partial")
            || Existing->State == TEXT("outcome_unknown"))
        {
            Admission.Kind = EUnrealMCPOperationAdmission::ReplaySuccess;
            Admission.Result = Existing->Result;
        }
        else if (Existing->State == TEXT("rejected") || Existing->State == TEXT("cancelled"))
        {
            Admission.Kind = EUnrealMCPOperationAdmission::ReplayError;
            Admission.OwnedError = Existing->Error;
            Admission.Error = Admission.OwnedError.Get();
        }
        else
        {
            Admission.Kind = EUnrealMCPOperationAdmission::Busy;
            Admission.OwnedError = MakeShared<FUnrealMCPError>(FUnrealMCPError{TEXT("busy"), TEXT("The retained operation is already queued or executing"), MakeShared<FUnrealMCPRecord>(), true});
            Admission.Error = Admission.OwnedError.Get();
        }
        return Admission;
    }
    if (!MakeRoomLocked())
    {
        Admission.Kind = EUnrealMCPOperationAdmission::Busy;
        Admission.OwnedError = MakeShared<FUnrealMCPError>(FUnrealMCPError{TEXT("busy"), TEXT("The retained operation ledger is full"), MakeShared<FUnrealMCPRecord>(), true});
        Admission.Error = Admission.OwnedError.Get();
        return Admission;
    }
    Entries.Add(OperationId, FEntry{Command, Admission.RequestDigest, TEXT("queued"), CurrentTime,
        CurrentTime + UnrealMCP::OperationLifetimeSeconds});
    Admission.Kind = EUnrealMCPOperationAdmission::Accepted;
    return Admission;
}

bool FUnrealMCPOperationLedger::MarkExecuting(const FString& OperationId, FUnrealMCPError& OutError)
{
    FScopeLock Lock(&Mutex);
    FEntry* Entry = Entries.Find(OperationId);
    if (Entry == nullptr)
    {
        OutError = {TEXT("outcome_unknown"), TEXT("The operation is no longer retained")};
        return false;
    }
    if (Entry->State == TEXT("cancelled"))
    {
        OutError = Entry->Error.IsValid() ? *Entry->Error : FUnrealMCPError{TEXT("cancelled"), TEXT("The queued operation was cancelled")};
        return false;
    }
    if (Entry->State != TEXT("queued"))
    {
        OutError = {TEXT("busy"), TEXT("The operation is not queued"), MakeShared<FUnrealMCPRecord>(), true};
        return false;
    }
    Entry->State = TEXT("executing");
    return true;
}

void FUnrealMCPOperationLedger::Commit(const FString& OperationId, const TSharedPtr<FUnrealMCPRecord>& Result)
{
    Complete(OperationId, TEXT("committed"), Result);
}

void FUnrealMCPOperationLedger::Complete(
    const FString& OperationId,
    const FString& State,
    const TSharedPtr<FUnrealMCPRecord>& Result)
{
    FScopeLock Lock(&Mutex);
    if (FEntry* Entry = Entries.Find(OperationId))
    {
        Entry->State =
            State == TEXT("partial") || State == TEXT("outcome_unknown")
            ? State
            : TEXT("committed");
        Entry->Result = Result;
        Entry->Error.Reset();
        Entry->ExpiresAt = Now() + UnrealMCP::OperationLifetimeSeconds;
    }
}

void FUnrealMCPOperationLedger::Reject(const FString& OperationId, const FUnrealMCPError& Error)
{
    FScopeLock Lock(&Mutex);
    if (FEntry* Entry = Entries.Find(OperationId))
    {
        Entry->State = Error.Code == TEXT("cancelled") ? TEXT("cancelled") : TEXT("rejected");
        Entry->Error = MakeShared<FUnrealMCPError>(Error);
        Entry->Result.Reset();
        Entry->ExpiresAt = Now() + UnrealMCP::OperationLifetimeSeconds;
    }
}

TSharedRef<FUnrealMCPRecord> FUnrealMCPOperationLedger::EntryStatusLocked(const FString& OperationId, const FEntry& Entry) const
{
    const TSharedRef<FUnrealMCPRecord> Value = MakeShared<FUnrealMCPRecord>();
    Value->SetStringField(TEXT("operation_id"), OperationId);
    Value->SetStringField(TEXT("bridge_instance_id"), BridgeInstanceId);
    Value->SetStringField(TEXT("command"), Entry.Command);
    Value->SetStringField(TEXT("request_digest"), Entry.Digest);
    Value->SetStringField(TEXT("state"), Entry.State);
    Value->SetBoolField(TEXT("retained"), true);
    if ((Entry.State == TEXT("committed") || Entry.State == TEXT("partial")
            || Entry.State == TEXT("outcome_unknown"))
        && Entry.Result.IsValid())
    {
        Value->SetObjectField(TEXT("result"), Entry.Result);
        Value->SetBoolField(TEXT("retry_safe"), false);
    }
    if ((Entry.State == TEXT("rejected") || Entry.State == TEXT("cancelled")) && Entry.Error.IsValid())
    {
        Value->SetObjectField(TEXT("error"), ErrorValue(*Entry.Error));
    }
    return Value;
}

bool FUnrealMCPOperationLedger::Status(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    FString OperationId;
    FString RequestedInstance;
    if (!ParseOperationIdentity(
        Arguments,
        TEXT("operation_status"),
        OperationId,
        RequestedInstance,
        OutError))
    {
        return false;
    }
    FScopeLock Lock(&Mutex);
    RemoveExpiredLocked(Now());
    if (RequestedInstance != BridgeInstanceId || !Entries.Contains(OperationId))
    {
        OutResult = UnknownOperationStatus(OperationId, BridgeInstanceId);
        return true;
    }
    OutResult = EntryStatusLocked(OperationId, Entries[OperationId]);
    return true;
}

bool FUnrealMCPOperationLedger::Cancel(
    const TSharedPtr<FUnrealMCPRecord>& Arguments,
    TSharedPtr<FUnrealMCPRecord>& OutResult,
    FUnrealMCPError& OutError)
{
    FString OperationId;
    FString RequestedInstance;
    if (!ParseOperationIdentity(
        Arguments,
        TEXT("operation_cancel"),
        OperationId,
        RequestedInstance,
        OutError))
    {
        return false;
    }
    FScopeLock Lock(&Mutex);
    RemoveExpiredLocked(Now());
    if (RequestedInstance != BridgeInstanceId || !Entries.Contains(OperationId))
    {
        OutResult = UnknownOperationStatus(OperationId, BridgeInstanceId);
        OutResult->SetBoolField(TEXT("cancelled"), false);
        return true;
    }
    FEntry& Entry = Entries[OperationId];
    if (Entry.State == TEXT("queued"))
    {
        Entry.State = TEXT("cancelled");
        Entry.Error = MakeShared<FUnrealMCPError>(FUnrealMCPError{TEXT("cancelled"), TEXT("The queued operation was cancelled")});
        Entry.ExpiresAt = Now() + UnrealMCP::OperationLifetimeSeconds;
    }
    OutResult = EntryStatusLocked(OperationId, Entry);
    OutResult->SetBoolField(TEXT("cancelled"), Entry.State == TEXT("cancelled"));
    return true;
}

void FUnrealMCPOperationLedger::CancelQueued()
{
    FScopeLock Lock(&Mutex);
    for (TPair<FString, FEntry>& Pair : Entries)
    {
        if (Pair.Value.State == TEXT("queued"))
        {
            Pair.Value.State = TEXT("cancelled");
            Pair.Value.Error = MakeShared<FUnrealMCPError>(FUnrealMCPError{TEXT("cancelled"), TEXT("Bridge shutdown cancelled queued operation")});
        }
    }
}

TSharedPtr<FUnrealMCPRecord> FUnrealMCPOperationLedger::CurrentState() const
{
    FScopeLock Lock(&Mutex);
    const TSharedRef<FUnrealMCPRecord> State = MakeShared<FUnrealMCPRecord>();
    int32 Queued = 0;
    int32 Executing = 0;
    for (const TPair<FString, FEntry>& Pair : Entries)
    {
        Queued += Pair.Value.State == TEXT("queued") ? 1 : 0;
        Executing += Pair.Value.State == TEXT("executing") ? 1 : 0;
    }
    State->SetStringField(TEXT("state"), Executing > 0 ? TEXT("executing") : Queued > 0 ? TEXT("queued") : TEXT("idle"));
    State->SetNumberField(TEXT("queued"), Queued);
    State->SetNumberField(TEXT("executing"), Executing);
    State->SetNumberField(TEXT("retained"), Entries.Num());
    return State;
}
