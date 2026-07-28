#include "UnrealMCPBlueprintGraphResultBuilder.h"

#include "UnrealMCPBlueprintFamilyPolicy.h"
#include "UnrealMCPBlueprintInspectionSupport.h"
#include "UnrealMCPK2TypeCodec.h"
#include "UnrealMCPBlueprintMutationCommon.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "UnrealMCPVersion.h"
#include "UObject/Package.h"

namespace UnrealMCP::BlueprintGraphResultBuilder
{
using UnrealMCP::BlueprintInspectionPrivate::IsStructuralGraphPin;
using UnrealMCP::BlueprintInspectionPrivate::StructuralGraphPinCount;
using UnrealMCP::BlueprintMutationPrivate::GuidString;

namespace
{
FString PinDefaultText(const UEdGraphPin* Pin)
{
    if (Pin == nullptr) return FString();
    if (Pin->DefaultObject != nullptr) return Pin->DefaultObject->GetPathName();
    if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text) return Pin->DefaultTextValue.ToString();
    return Pin->DefaultValue;
}

TArray<TSharedPtr<FJsonValue>> EncodeIdentities(const TSet<FString>& Identities)
{
    TArray<FString> Sorted = Identities.Array();
    Sorted.Sort();
    TArray<TSharedPtr<FJsonValue>> Result;
    Result.Reserve(Sorted.Num());
    for (const FString& Id : Sorted) Result.Add(MakeShared<FJsonValueString>(Id));
    return Result;
}
}

TSharedRef<FJsonObject> EncodeNode(UEdGraph* Graph, UEdGraphNode* Node)
{
    const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
    Record->SetStringField(TEXT("graph_id"), Graph != nullptr ? GuidString(Graph->GraphGuid) : FString());
    Record->SetStringField(TEXT("id"), Node != nullptr ? GuidString(Node->NodeGuid) : FString());
    Record->SetBoolField(TEXT("identity_stable"), Node != nullptr && Node->NodeGuid.IsValid());
    Record->SetStringField(TEXT("class_path"), Node != nullptr ? Node->GetClass()->GetPathName() : FString());
    Record->SetStringField(TEXT("title"), Node != nullptr ? Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Left(256) : FString());
    Record->SetNumberField(TEXT("x"), Node != nullptr ? Node->NodePosX : 0);
    Record->SetNumberField(TEXT("y"), Node != nullptr ? Node->NodePosY : 0);
    TArray<TSharedPtr<FJsonValue>> Pins;
    if (Node != nullptr)
    {
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsStructuralGraphPin(Node, Pin) || Pins.Num() >= UnrealMCP::MaxGraphPinsPerNode) continue;
            const TSharedRef<FJsonObject> PinRecord = MakeShared<FJsonObject>();
            PinRecord->SetStringField(TEXT("id"), GuidString(Pin->PinId));
            PinRecord->SetBoolField(TEXT("identity_stable"), Pin->PinId.IsValid());
            PinRecord->SetStringField(TEXT("name"), Pin->PinName.ToString().Left(128));
            PinRecord->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
            PinRecord->SetObjectField(TEXT("type"), UnrealMCP::BlueprintInspectionPrivate::PinType(Pin->PinType));
            PinRecord->SetStringField(TEXT("default_value"), Pin->DefaultValue.Left(512));
            if (Pin->DefaultObject != nullptr) PinRecord->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName().Left(512));
            if (!Pin->DefaultTextValue.IsEmpty()) PinRecord->SetStringField(TEXT("default_text"), Pin->DefaultTextValue.ToString().Left(512));
            PinRecord->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(Pin->PinType, PinDefaultText(Pin)));
            Pins.Add(MakeShared<FJsonValueObject>(PinRecord));
        }
    }
    Record->SetArrayField(TEXT("pins"), Pins);
    Record->SetNumberField(TEXT("pin_count"), StructuralGraphPinCount(Node));
    return Record;
}

TSharedRef<FJsonObject> Build(
    UBlueprint* Blueprint,
    const BlueprintGraphRequestValidation::FRequest& Request,
    const FString& Snapshot,
    const TSharedRef<FJsonObject>& Changed,
    const TSet<FString>& CreatedIdentities,
    const TSet<FString>& ReconstructedIdentities)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), Request.AssetPath);
    Result->SetStringField(TEXT("blueprint_family"), UnrealMCP::BlueprintFamilyPolicy::Classify(Blueprint->ParentClass).Name);
    Result->SetObjectField(TEXT("family_capabilities"), UnrealMCP::BlueprintFamilyPolicy::BuildLiveCapabilities(Blueprint));
    Result->SetStringField(TEXT("edit"), Request.Operation);
    Result->SetStringField(TEXT("graph_id"), Request.GraphId);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    Result->SetBoolField(TEXT("package_dirty"), Blueprint->GetOutermost()->IsDirty());
    Result->SetObjectField(TEXT("changed"), Changed);
    Result->SetArrayField(TEXT("created_identities"), EncodeIdentities(CreatedIdentities));
    Result->SetArrayField(TEXT("reconstructed_identities"), EncodeIdentities(ReconstructedIdentities));
    return Result;
}
}
