#pragma once

#include "UnrealMCPBlueprintActionCatalogSupport.h"

namespace UnrealMCP::BlueprintActionCatalogPrivate
{
struct FActionScanResult
{
    TArray<TSharedPtr<FJsonObject>> CandidateRecords;
    int32 ScannedCount = 0;
    bool bTimedOut = false;
    bool bScanLimited = false;
    bool bResultLimited = false;
};

static FActionScanResult ScanActions(
    UBlueprint* Blueprint,
    UEdGraph* Graph,
    UEdGraphPin* ContextPin,
    const FString& Text,
    const FString& OwnerClassFilter,
    const FString& FunctionFilter,
    const FString& MemberFilter,
    const FString& FamilyFilter,
    int32 Limit,
    const TFunction<double()>& ScanNow)
{
FBlueprintActionFilter Filter;
Filter.Context.Blueprints.Add(Blueprint);
Filter.Context.Graphs.Add(Graph);
if (ContextPin != nullptr) Filter.Context.Pins.Add(ContextPin);
const double StartedAt = ScanNow();
FBlueprintActionDatabase& Database = FBlueprintActionDatabase::Get();
const FBlueprintActionDatabase::FActionRegistry& RegistryActions = Database.GetAllActions();
TArray<TSharedPtr<FJsonObject>> CandidateRecords;
TSet<FString> Signatures;
int32 ScannedCount = 0;
bool bTimedOut = false;
bool bScanLimited = false;
auto ProcessActions = [&](UObject* ActionOwner, const FBlueprintActionDatabase::FActionList& ActionList)
{
    if (ActionOwner == nullptr) return;
    for (const UBlueprintNodeSpawner* Spawner : ActionList)
    {
        if (++ScannedCount > UnrealMCP::MaxActionScan) { bScanLimited = true; break; }
        if (ScanNow() - StartedAt > UnrealMCP::ActionScanSeconds) { bTimedOut = true; break; }
        FString Family;
        if (Spawner == nullptr || !IsCoreFamily(Spawner, Family) || (!FamilyFilter.IsEmpty() && Family != FamilyFilter)) continue;
        FBlueprintActionInfo ActionInfo(ActionOwner, Spawner);
        if (Filter.IsFiltered(ActionInfo)) continue;
        const UFunction* Function = ActionInfo.GetAssociatedFunction();
        const FProperty* Property = ActionInfo.GetAssociatedProperty();
        if (Family == TEXT("function_call") && Function == nullptr) continue;
        if (Family != TEXT("function_call") && Property == nullptr) continue;
        const FString MemberName = Function != nullptr ? Function->GetName() : Property->GetName();
        const UClass* OwnerClass = Function != nullptr ? Function->GetOwnerClass() : Property->GetOwnerClass();
        const FString OwnerPath = CanonicalOwnerPath(OwnerClass, Blueprint);
        if (!OwnerClassFilter.IsEmpty() && OwnerPath != OwnerClassFilter) continue;
        if (!FunctionFilter.IsEmpty() && (Function == nullptr || !MemberName.Equals(FunctionFilter, ESearchCase::IgnoreCase))) continue;
        if (!MemberFilter.IsEmpty() && (Property == nullptr || !MemberName.Equals(MemberFilter, ESearchCase::IgnoreCase))) continue;
        const FBlueprintActionUiSpec Ui = Spawner->GetUiSpec(Filter.Context, ActionInfo.GetBindings());
        const FString Title = Ui.MenuName.ToString().Left(256);
        if (!Text.IsEmpty() && !Title.Equals(Text, ESearchCase::IgnoreCase) && !MemberName.Equals(Text, ESearchCase::IgnoreCase)) continue;
        const FString Signature = ActionSignature(Family, OwnerPath, MemberName, Spawner);
        if (Signatures.Contains(Signature)) continue;
        Signatures.Add(Signature);
        const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
        Record->SetStringField(TEXT("_rebuild_signature"), Signature);
        Record->SetStringField(TEXT("node_family"), Family);
        Record->SetStringField(TEXT("title"), Title);
        Record->SetStringField(TEXT("category"), Ui.Category.ToString().Left(256));
        Record->SetStringField(TEXT("owner_class"), OwnerPath);
        Record->SetStringField(TEXT("member_name"), MemberName);
        Record->SetStringField(TEXT("member_kind"), Function != nullptr ? TEXT("function") : TEXT("variable"));
        if (Function != nullptr)
        {
            Record->SetBoolField(TEXT("pure"), Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
            Record->SetBoolField(TEXT("static"), Function->HasAnyFunctionFlags(FUNC_Static));
            Record->SetBoolField(TEXT("const"), Function->HasAnyFunctionFlags(FUNC_Const));
        }
        CandidateRecords.Add(Record);
    }
};
TSet<FObjectKey> ProcessedOwners;
TArray<UObject*> PriorityOwners = {Blueprint, Blueprint->SkeletonGeneratedClass, Blueprint->GeneratedClass};
for (UClass* Class = Blueprint->GeneratedClass; Class != nullptr; Class = Class->GetSuperClass()) PriorityOwners.AddUnique(Class);
for (UClass* Class = Blueprint->SkeletonGeneratedClass; Class != nullptr; Class = Class->GetSuperClass()) PriorityOwners.AddUnique(Class);
for (UObject* Owner : PriorityOwners)
{
    if (Owner == nullptr) continue;
    const FObjectKey Key(Owner);
    if (const FBlueprintActionDatabase::FActionList* Actions = RegistryActions.Find(Key))
    {
        ProcessedOwners.Add(Key);
        ProcessActions(Owner, *Actions);
    }
    if (bScanLimited || bTimedOut) break;
}
if (!bScanLimited && !bTimedOut)
{
    for (auto It = RegistryActions.CreateConstIterator(); It; ++It)
    {
        if (ProcessedOwners.Contains(It.Key())) continue;
        ProcessActions(It.Key().ResolveObjectPtr(), It.Value());
        if (bScanLimited || bTimedOut) break;
    }
}
CandidateRecords.Sort([](const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right)
{
    const FString A = Left->GetStringField(TEXT("node_family")) + TEXT("|") + Left->GetStringField(TEXT("owner_class"))
        + TEXT("|") + Left->GetStringField(TEXT("member_name")) + TEXT("|") + Left->GetStringField(TEXT("title"));
    const FString B = Right->GetStringField(TEXT("node_family")) + TEXT("|") + Right->GetStringField(TEXT("owner_class"))
        + TEXT("|") + Right->GetStringField(TEXT("member_name")) + TEXT("|") + Right->GetStringField(TEXT("title"));
    return A < B;
});
const bool bResultLimited = CandidateRecords.Num() > Limit;
if (CandidateRecords.Num() > Limit) CandidateRecords.SetNum(Limit);
    FActionScanResult Result;
    Result.CandidateRecords = MoveTemp(CandidateRecords);
    Result.ScannedCount = ScannedCount;
    Result.bTimedOut = bTimedOut;
    Result.bScanLimited = bScanLimited;
    Result.bResultLimited = bResultLimited;
    return Result;
}
}

