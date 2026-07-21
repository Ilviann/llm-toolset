#include "UnrealMCPOperationLedger.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
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
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    Writer->WriteValue(Value);
    Writer->Close();
    return Output;
}

FString CanonicalValue(const TSharedPtr<FJsonValue>& Value);

FString CanonicalObject(const TSharedPtr<FJsonObject>& Object)
{
    if (!Object.IsValid()) return TEXT("null");
    TArray<TPair<FString, TSharedPtr<FJsonValue>>> Values;
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

FString CanonicalValue(const TSharedPtr<FJsonValue>& Value)
{
    if (!Value.IsValid()) return TEXT("null");
    switch (Value->Type)
    {
    case EJson::Null: return TEXT("null");
    case EJson::String: return QuoteJsonString(Value->AsString());
    case EJson::Boolean: return Value->AsBool() ? TEXT("true") : TEXT("false");
    case EJson::Number: return FString::Printf(TEXT("%.17g"), Value->AsNumber());
    case EJson::Object: return CanonicalObject(Value->AsObject());
    case EJson::Array:
        {
            TArray<FString> Items;
            for (const TSharedPtr<FJsonValue>& Item : Value->AsArray()) Items.Add(CanonicalValue(Item));
            return TEXT("[") + FString::Join(Items, TEXT(",")) + TEXT("]");
        }
    default: return TEXT("null");
    }
}

TSharedRef<FJsonObject> ErrorValue(const FUnrealMCPError& Error)
{
    const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
    Value->SetStringField(TEXT("code"), Error.Code.Left(64));
    Value->SetStringField(TEXT("message"), Error.Message.Left(512));
    Value->SetObjectField(TEXT("details"), Error.Details.IsValid() ? Error.Details : MakeShared<FJsonObject>());
    Value->SetBoolField(TEXT("retryable"), Error.bRetryable);
    return Value;
}
}

FUnrealMCPOperationLedger::FUnrealMCPOperationLedger(FString InBridgeInstanceId, FString InContextBinding, TFunction<double()> InNow)
    : BridgeInstanceId(MoveTemp(InBridgeInstanceId)), ContextBinding(MoveTemp(InContextBinding)), Now(MoveTemp(InNow))
{
}

FString FUnrealMCPOperationLedger::DigestRequest(
    const FString& Command,
    const TSharedPtr<FJsonObject>& Arguments,
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

FUnrealMCPOperationAdmission FUnrealMCPOperationLedger::Admit(const FString& Command, const TSharedPtr<FJsonObject>& Arguments)
{
    FUnrealMCPOperationAdmission Admission;
    FString OperationId;
    if (!Arguments.IsValid() || !Arguments->TryGetStringField(TEXT("operation_id"), OperationId) || !IsOperationId(OperationId))
    {
        Admission.Kind = EUnrealMCPOperationAdmission::Conflict;
        Admission.OwnedError = MakeShared<FUnrealMCPError>(FUnrealMCPError{TEXT("invalid_argument"), TEXT("Every mutation requires one 32-character lowercase hexadecimal operation_id")});
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
        else if (Existing->State == TEXT("committed"))
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
            Admission.OwnedError = MakeShared<FUnrealMCPError>(FUnrealMCPError{TEXT("busy"), TEXT("The mutation operation is already queued or executing"), MakeShared<FJsonObject>(), true});
            Admission.Error = Admission.OwnedError.Get();
        }
        return Admission;
    }
    if (!MakeRoomLocked())
    {
        Admission.Kind = EUnrealMCPOperationAdmission::Busy;
        Admission.OwnedError = MakeShared<FUnrealMCPError>(FUnrealMCPError{TEXT("busy"), TEXT("The mutation operation ledger is full"), MakeShared<FJsonObject>(), true});
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
        OutError = Entry->Error.IsValid() ? *Entry->Error : FUnrealMCPError{TEXT("cancelled"), TEXT("The queued mutation was cancelled")};
        return false;
    }
    if (Entry->State != TEXT("queued"))
    {
        OutError = {TEXT("busy"), TEXT("The operation is not queued"), MakeShared<FJsonObject>(), true};
        return false;
    }
    Entry->State = TEXT("executing");
    return true;
}

void FUnrealMCPOperationLedger::Commit(const FString& OperationId, const TSharedPtr<FJsonObject>& Result)
{
    FScopeLock Lock(&Mutex);
    if (FEntry* Entry = Entries.Find(OperationId))
    {
        Entry->State = TEXT("committed");
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

TSharedRef<FJsonObject> FUnrealMCPOperationLedger::EntryStatusLocked(const FString& OperationId, const FEntry& Entry) const
{
    const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
    Value->SetStringField(TEXT("operation_id"), OperationId);
    Value->SetStringField(TEXT("bridge_instance_id"), BridgeInstanceId);
    Value->SetStringField(TEXT("command"), Entry.Command);
    Value->SetStringField(TEXT("request_digest"), Entry.Digest);
    Value->SetStringField(TEXT("state"), Entry.State);
    Value->SetBoolField(TEXT("retained"), true);
    if (Entry.State == TEXT("committed") && Entry.Result.IsValid()) Value->SetObjectField(TEXT("result"), Entry.Result);
    if ((Entry.State == TEXT("rejected") || Entry.State == TEXT("cancelled")) && Entry.Error.IsValid())
    {
        Value->SetObjectField(TEXT("error"), ErrorValue(*Entry.Error));
    }
    return Value;
}

bool FUnrealMCPOperationLedger::Status(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    if (!Arguments.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("arguments must be an object")};
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments->Values)
    {
        if (Pair.Key != TEXT("operation_id") && Pair.Key != TEXT("bridge_instance_id") && Pair.Key != TEXT("cancel"))
        {
            OutError = {TEXT("invalid_argument"), TEXT("operation_status contains an unknown field")};
            return false;
        }
    }
    FString OperationId;
    FString RequestedInstance;
    bool bCancel = false;
    if (!Arguments->TryGetStringField(TEXT("operation_id"), OperationId) || !IsOperationId(OperationId)
        || !Arguments->TryGetStringField(TEXT("bridge_instance_id"), RequestedInstance) || !IsOperationId(RequestedInstance)
        || (Arguments->HasField(TEXT("cancel")) && !Arguments->TryGetBoolField(TEXT("cancel"), bCancel)))
    {
        OutError = {TEXT("invalid_argument"), TEXT("operation_status requires valid operation_id, bridge_instance_id, and optional boolean cancel")};
        return false;
    }
    FScopeLock Lock(&Mutex);
    RemoveExpiredLocked(Now());
    if (RequestedInstance != BridgeInstanceId || !Entries.Contains(OperationId))
    {
        const TSharedRef<FJsonObject> Unknown = MakeShared<FJsonObject>();
        Unknown->SetStringField(TEXT("operation_id"), OperationId);
        Unknown->SetStringField(TEXT("bridge_instance_id"), BridgeInstanceId);
        Unknown->SetStringField(TEXT("state"), TEXT("outcome_unknown"));
        Unknown->SetBoolField(TEXT("retained"), false);
        Unknown->SetBoolField(TEXT("retry_safe"), false);
        OutResult = Unknown;
        return true;
    }
    FEntry& Entry = Entries[OperationId];
    if (bCancel && Entry.State == TEXT("queued"))
    {
        Entry.State = TEXT("cancelled");
        Entry.Error = MakeShared<FUnrealMCPError>(FUnrealMCPError{TEXT("cancelled"), TEXT("The queued mutation was cancelled")});
        Entry.ExpiresAt = Now() + UnrealMCP::OperationLifetimeSeconds;
    }
    OutResult = EntryStatusLocked(OperationId, Entry);
    if (bCancel) OutResult->SetBoolField(TEXT("cancelled"), Entry.State == TEXT("cancelled"));
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
            Pair.Value.Error = MakeShared<FUnrealMCPError>(FUnrealMCPError{TEXT("cancelled"), TEXT("Bridge shutdown cancelled queued mutation")});
        }
    }
}

TSharedPtr<FJsonObject> FUnrealMCPOperationLedger::CurrentState() const
{
    FScopeLock Lock(&Mutex);
    const TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
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
