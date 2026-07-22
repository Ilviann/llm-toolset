#include "UnrealMCPBlueprintCallableMutationSupport.h"

namespace
{
const TMap<FString, FString> ReplicationProperties = {
    {TEXT("replicates"), TEXT("bReplicates")}, {TEXT("replicate_movement"), TEXT("bReplicateMovement")},
    {TEXT("always_relevant"), TEXT("bAlwaysRelevant")}, {TEXT("only_relevant_to_owner"), TEXT("bOnlyRelevantToOwner")},
    {TEXT("use_owner_relevancy"), TEXT("bNetUseOwnerRelevancy")}, {TEXT("dormancy"), TEXT("NetDormancy")},
    {TEXT("net_priority"), TEXT("NetPriority")}, {TEXT("net_update_frequency"), TEXT("NetUpdateFrequency")},
    {TEXT("min_net_update_frequency"), TEXT("MinNetUpdateFrequency")}};

bool ReadBoolProperty(UObject* Object, const TCHAR* Name, bool& Out)
{
    const FBoolProperty* Property = Object != nullptr ? FindFProperty<FBoolProperty>(Object->GetClass(), Name) : nullptr;
    if (Property == nullptr) return false;
    Out = Property->GetPropertyValue_InContainer(Object);
    return true;
}

bool ReadNumberProperty(UObject* Object, const TCHAR* Name, double& Out)
{
    const FNumericProperty* Property = Object != nullptr ? FindFProperty<FNumericProperty>(Object->GetClass(), Name) : nullptr;
    if (Property == nullptr) return false;
    const void* Address = Property->ContainerPtrToValuePtr<void>(Object);
    Out = Property->IsFloatingPoint() ? Property->GetFloatingPointPropertyValue(Address)
        : static_cast<double>(Property->GetSignedIntPropertyValue(Address));
    return true;
}
}


bool FUnrealMCPBlueprintMutator::ComponentEdit(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    if (!RequireMutationPreconditions(*Arguments, OutError)) return false;
    FString Operation;
    if (!Arguments->TryGetStringField(TEXT("operation"), Operation))
    {
        OutError = {TEXT("invalid_argument"), TEXT("blueprint_component_edit requires one typed operation")};
        return false;
    }
    TSet<FString> Allowed = {TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("operation")};
    if (Operation == TEXT("add")) Allowed.Append({TEXT("component_class"), TEXT("name"), TEXT("parent_id")});
    else if (Operation == TEXT("remove")) Allowed.Add(TEXT("component_id"));
    else if (Operation == TEXT("rename")) Allowed.Append({TEXT("component_id"), TEXT("new_name")});
    else if (Operation == TEXT("reparent")) Allowed.Append({TEXT("component_id"), TEXT("new_parent_id")});
    else if (Operation == TEXT("set_root")) Allowed.Add(TEXT("component_id"));
    else if (Operation == TEXT("set_property")) Allowed.Append({TEXT("component_id"), TEXT("property_name"), TEXT("value")});
    else if (Operation == TEXT("set_replication")) Allowed.Append({TEXT("component_id"), TEXT("replicates")});
    else
    {
        OutError = {TEXT("invalid_argument"), TEXT("Unknown component edit operation")};
        return false;
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments->Values)
    {
        if (!Allowed.Contains(Pair.Key))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The component edit contains a field not accepted by its operation")};
            return false;
        }
    }

    FString RawAsset;
    if (!Arguments->TryGetStringField(TEXT("asset_path"), RawAsset))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_path must identify one exact Blueprint asset")};
        return false;
    }
    const TSharedRef<FJsonObject> AssetOnly = MakeShared<FJsonObject>();
    AssetOnly->SetStringField(TEXT("asset_path"), RawAsset);
    UBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    FString PackageName;
    if (!ResolveMutableBlueprint(*AssetOnly, Blueprint, ObjectPath, PackageName, OutError,
        UnrealMCP::BlueprintFamilyPolicy::EOperation::Components)
        || !ValidateExpectedSnapshot(Inspector, *Arguments, ObjectPath, OutError))
    {
        return false;
    }
    USubobjectDataSubsystem* Subsystem = USubobjectDataSubsystem::Get();
    FComponentHandles Handles;
    if (!GatherComponentHandles(Blueprint, Subsystem, Handles, OutError)) return false;

    FString ComponentId;
    FString NewParentId;
    FString NewName;
    FString ComponentClassPath;
    FString PropertyName;
    bool bReplicates = false;
    USCS_Node* Node = nullptr;
    USCS_Node* ParentNode = nullptr;
    FSubobjectDataHandle Handle;
    FSubobjectDataHandle ParentHandle = Handles.Context;
    UClass* ComponentClass = nullptr;

    if (Operation == TEXT("add"))
    {
        if (!Arguments->TryGetStringField(TEXT("component_class"), ComponentClassPath)
            || !Arguments->TryGetStringField(TEXT("name"), NewName) || !IsLegalComponentName(NewName)
            || !ResolveComponentClass(ComponentClassPath, ComponentClass, OutError))
        {
            if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_component"), TEXT("add requires a valid spawnable component_class and unique legal name")};
            return false;
        }
        if (FindLocalNodeByName(Blueprint, NewName) != nullptr)
        {
            OutError = {TEXT("invalid_component"), TEXT("A local component already uses that exact name")};
            return false;
        }
        if (Arguments->HasField(TEXT("parent_id")))
        {
            if (!Arguments->TryGetStringField(TEXT("parent_id"), NewParentId)
                || (ParentNode = FindLocalNode(Blueprint, NewParentId)) == nullptr || !Handles.ById.Contains(NewParentId))
            {
                OutError = {TEXT("stale_precondition"), TEXT("The requested local parent component identity is unavailable")};
                return false;
            }
            if (!ComponentClass->IsChildOf(USceneComponent::StaticClass())
                || ParentNode->ComponentClass == nullptr || !ParentNode->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
            {
                OutError = {TEXT("invalid_component"), TEXT("Only scene components can use a scene-component parent")};
                return false;
            }
            ParentHandle = Handles.ById[NewParentId];
        }
    }
    else
    {
        if (!Arguments->TryGetStringField(TEXT("component_id"), ComponentId)
            || (Node = FindLocalNode(Blueprint, ComponentId)) == nullptr || !Handles.ById.Contains(ComponentId))
        {
            OutError = {TEXT("stale_precondition"), TEXT("The requested stable local component identity is unavailable")};
            return false;
        }
        Handle = Handles.ById[ComponentId];
        const FSubobjectData* Data = Handle.GetData();
        if (Data == nullptr || Data->IsInheritedComponent() || Data->IsNativeComponent() || Data->IsInstancedComponent())
        {
            OutError = {TEXT("invalid_component"), TEXT("Only locally owned SCS components are mutable")};
            return false;
        }
    }

    if (Operation == TEXT("remove"))
    {
        if (!Node->GetChildNodes().IsEmpty())
        {
            OutError = {TEXT("invalid_component"), TEXT("Reparent or remove child components before removing their parent")};
            return false;
        }
        if (Blueprint->SimpleConstructionScript->GetRootNodes().Contains(Node))
        {
            for (USCS_Node* Candidate : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                if (Candidate != Node && Candidate != nullptr && Candidate->ComponentClass != nullptr
                    && Candidate->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
                {
                    OutError = {TEXT("invalid_component"), TEXT("Select another scene root before removing the current root")};
                    return false;
                }
            }
        }
    }
    else if (Operation == TEXT("rename"))
    {
        if (!Arguments->TryGetStringField(TEXT("new_name"), NewName) || !IsLegalComponentName(NewName))
        {
            OutError = {TEXT("invalid_component"), TEXT("new_name must be one legal bounded component name")};
            return false;
        }
        USCS_Node* Existing = FindLocalNodeByName(Blueprint, NewName);
        if (Existing != nullptr && Existing != Node)
        {
            OutError = {TEXT("invalid_component"), TEXT("A local component already uses that exact name")};
            return false;
        }
    }
    else if (Operation == TEXT("reparent"))
    {
        if (!Arguments->TryGetStringField(TEXT("new_parent_id"), NewParentId)
            || (ParentNode = FindLocalNode(Blueprint, NewParentId)) == nullptr || !Handles.ById.Contains(NewParentId))
        {
            OutError = {TEXT("stale_precondition"), TEXT("The requested new parent identity is unavailable")};
            return false;
        }
        if (Node == ParentNode || ParentNode->IsChildOf(Node)
            || Node->ComponentClass == nullptr || ParentNode->ComponentClass == nullptr
            || !Node->ComponentClass->IsChildOf(USceneComponent::StaticClass())
            || !ParentNode->ComponentClass->IsChildOf(USceneComponent::StaticClass()))
        {
            OutError = {TEXT("invalid_component"), TEXT("Reparenting requires two distinct local scene components and must not create a cycle")};
            return false;
        }
        ParentHandle = Handles.ById[NewParentId];
    }
    else if (Operation == TEXT("set_root")
        && (Node->ComponentClass == nullptr || !Node->ComponentClass->IsChildOf(USceneComponent::StaticClass())))
    {
        OutError = {TEXT("invalid_component"), TEXT("Only a local scene component can become the Actor root")};
        return false;
    }
    else if (Operation == TEXT("set_property"))
    {
        if (!Arguments->TryGetStringField(TEXT("property_name"), PropertyName) || !Arguments->HasField(TEXT("value")))
        {
            OutError = {TEXT("invalid_argument"), TEXT("set_property requires one exact property_name and value")};
            return false;
        }
        if (PropertyName == TEXT("bReplicates"))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Use the typed set_replication operation for component replication")};
            return false;
        }
    }
    else if (Operation == TEXT("set_replication"))
    {
        if (!Arguments->TryGetBoolField(TEXT("replicates"), bReplicates)
            || !UnrealMCP::BlueprintFamilyPolicy::SupportsComponentReplication(Blueprint->ParentClass))
        {
            OutError = {TEXT("invalid_component"), TEXT("This Blueprint family does not support typed component replication")};
            return false;
        }
        bool bActorReplicates = false;
        UObject* Defaults = Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
        if (bReplicates && !ReadBoolProperty(Defaults, TEXT("bReplicates"), bActorReplicates))
        {
            OutError = {TEXT("unsupported_property"), TEXT("The live Actor default does not expose its replication setting")};
            return false;
        }
        if (bReplicates && !bActorReplicates)
        {
            OutError = {TEXT("invalid_component"), TEXT("The owning Actor default must replicate before a component can replicate")};
            return false;
        }
        PropertyName = TEXT("bReplicates");
    }

    bool bApplied = false;
    TSharedPtr<FJsonObject> Changed = MakeShared<FJsonObject>();
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP component edit")));
        Blueprint->Modify();
        Blueprint->SimpleConstructionScript->Modify();
        if (Operation == TEXT("add"))
        {
            FAddNewSubobjectParams Params;
            Params.ParentHandle = ParentHandle;
            Params.NewClass = ComponentClass;
            Params.BlueprintContext = Blueprint;
            FText Failure;
            FSubobjectDataHandle NewHandle = Subsystem->AddNewSubobject(Params, Failure);
            bApplied = NewHandle.IsValid() && Subsystem->RenameSubobject(NewHandle, FText::FromString(NewName));
            Node = FindLocalNodeByName(Blueprint, NewName);
            bApplied = bApplied && Node != nullptr && Node->VariableGuid.IsValid() && Node->ComponentClass == ComponentClass;
            if (bApplied)
            {
                ComponentId = Node->VariableGuid.ToString(EGuidFormats::Digits).ToLower();
                Changed->SetStringField(TEXT("component_id"), ComponentId);
                Changed->SetStringField(TEXT("name"), NewName);
                Changed->SetStringField(TEXT("class_path"), ComponentClass->GetPathName());
                Changed->SetStringField(TEXT("parent_id"), NewParentId);
            }
        }
        else if (Operation == TEXT("remove"))
        {
            bApplied = Subsystem->DeleteSubobject(Handles.Context, Handle, Blueprint) == 1
                && FindLocalNode(Blueprint, ComponentId) == nullptr;
            Changed->SetStringField(TEXT("component_id"), ComponentId);
        }
        else if (Operation == TEXT("rename"))
        {
            bApplied = Subsystem->RenameSubobject(Handle, FText::FromString(NewName));
            Node = FindLocalNode(Blueprint, ComponentId);
            bApplied = bApplied && Node != nullptr && Node->GetVariableName().ToString() == NewName;
            Changed->SetStringField(TEXT("component_id"), ComponentId);
            Changed->SetStringField(TEXT("name"), NewName);
        }
        else if (Operation == TEXT("reparent"))
        {
            FReparentSubobjectParams Params;
            Params.NewParentHandle = ParentHandle;
            Params.BlueprintContext = Blueprint;
            Params.ActorPreviewContext = Blueprint->GeneratedClass != nullptr ? Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(false)) : nullptr;
            bApplied = Params.ActorPreviewContext != nullptr && Subsystem->ReparentSubobject(Params, Handle);
            Node = FindLocalNode(Blueprint, ComponentId);
            ParentNode = FindLocalNode(Blueprint, NewParentId);
            bApplied = bApplied && Node != nullptr && ParentNode != nullptr && ParentNode->GetChildNodes().Contains(Node);
            Changed->SetStringField(TEXT("component_id"), ComponentId);
            Changed->SetStringField(TEXT("parent_id"), NewParentId);
        }
        else if (Operation == TEXT("set_root"))
        {
            bApplied = Subsystem->MakeNewSceneRoot(Handles.Context, Handle, Blueprint);
            Node = FindLocalNode(Blueprint, ComponentId);
            bApplied = bApplied && Node != nullptr && Blueprint->SimpleConstructionScript->GetRootNodes().Contains(Node);
            Changed->SetStringField(TEXT("component_id"), ComponentId);
            Changed->SetBoolField(TEXT("root"), true);
        }
        else
        {
            Node->ComponentTemplate->Modify();
            bApplied = UnrealMCP::PropertyCodec::Set(
                Node->ComponentTemplate, PropertyName,
                Operation == TEXT("set_replication") ? MakeShared<FJsonValueBoolean>(bReplicates) : Arguments->Values.FindRef(TEXT("value")),
                Changed, OutError);
            if (bApplied)
            {
                FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
                Node = FindLocalNode(Blueprint, ComponentId);
                FProperty* LiveProperty = Node != nullptr && Node->ComponentTemplate != nullptr
                    ? Node->ComponentTemplate->GetClass()->FindPropertyByName(FName(*PropertyName)) : nullptr;
                bApplied = Node != nullptr && Node->ComponentTemplate != nullptr && LiveProperty != nullptr;
                if (bApplied) Changed = UnrealMCP::PropertyCodec::Encode(Node->ComponentTemplate, LiveProperty);
            }
            if (Changed.IsValid()) Changed->SetStringField(TEXT("component_id"), ComponentId);
        }
    }
    if (!bApplied)
    {
        RestoreFailedTransaction(OutError);
        if (OutError.Code.IsEmpty()) OutError = {TEXT("invalid_component"), TEXT("Unreal rejected the component edit without a committed change")};
        return false;
    }

    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutResult = BuildEditResult(Blueprint, ObjectPath, Snapshot, Operation, Changed,
        Operation == TEXT("add") ? TArray<FString>{ComponentId} : TArray<FString>{});
    return true;
}

bool FUnrealMCPBlueprintMutator::DefaultEdit(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::BlueprintMutationPrivate;
    if (!RequireMutationPreconditions(*Arguments, OutError)) return false;
    const bool bReplication = Arguments->HasField(TEXT("replication_setting"));
    if (!HasOnlyFields(*Arguments, bReplication
        ? std::initializer_list<const TCHAR*>{TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("replication_setting"), TEXT("value")}
        : std::initializer_list<const TCHAR*>{TEXT("operation_id"), TEXT("asset_path"), TEXT("expected_snapshot"), TEXT("property_name"), TEXT("value")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("blueprint_default_edit accepts one exact property or typed replication setting")};
        return false;
    }
    FString RawAsset;
    FString PropertyName;
    if (!Arguments->TryGetStringField(TEXT("asset_path"), RawAsset)
        || !(bReplication ? Arguments->TryGetStringField(TEXT("replication_setting"), PropertyName)
            : Arguments->TryGetStringField(TEXT("property_name"), PropertyName)) || !Arguments->HasField(TEXT("value")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("blueprint_default_edit requires asset_path, one setting, and value")};
        return false;
    }
    const TSharedRef<FJsonObject> AssetOnly = MakeShared<FJsonObject>();
    AssetOnly->SetStringField(TEXT("asset_path"), RawAsset);
    UBlueprint* Blueprint = nullptr;
    FString ObjectPath;
    FString PackageName;
    if (!ResolveMutableBlueprint(*AssetOnly, Blueprint, ObjectPath, PackageName, OutError,
        UnrealMCP::BlueprintFamilyPolicy::EOperation::ClassDefaults)
        || !ValidateExpectedSnapshot(Inspector, *Arguments, ObjectPath, OutError))
    {
        return false;
    }
    UObject* Defaults = Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
    if (Defaults == nullptr)
    {
        OutError = {TEXT("busy"), TEXT("The generated-class default object is unavailable"), MakeShared<FJsonObject>(), true};
        return false;
    }
    if (bReplication)
    {
        const FString* ReflectedName = ReplicationProperties.Find(PropertyName);
        if (ReflectedName == nullptr || !UnrealMCP::BlueprintFamilyPolicy::SupportsActorReplication(Blueprint->ParentClass))
        {
            OutError = {TEXT("unsupported_property"), TEXT("This Blueprint family does not support the selected Actor replication setting")};
            return false;
        }
        PropertyName = *ReflectedName;
        bool bValue = false;
        double Number = 0.0;
        if ((PropertyName == TEXT("bReplicates") || PropertyName == TEXT("bReplicateMovement")
                || PropertyName == TEXT("bAlwaysRelevant") || PropertyName == TEXT("bOnlyRelevantToOwner")
                || PropertyName == TEXT("bNetUseOwnerRelevancy"))
            && !Arguments->Values.FindRef(TEXT("value"))->TryGetBool(bValue))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The selected replication setting requires a Boolean value")};
            return false;
        }
        if ((PropertyName == TEXT("NetPriority") || PropertyName == TEXT("NetUpdateFrequency") || PropertyName == TEXT("MinNetUpdateFrequency"))
            && (!Arguments->Values.FindRef(TEXT("value"))->TryGetNumber(Number) || !FMath::IsFinite(Number)
                || Number < (PropertyName == TEXT("MinNetUpdateFrequency") ? 0.0 : 0.01) || Number > 1000.0))
        {
            OutError = {TEXT("invalid_argument"), TEXT("The selected replication rate or priority is outside its supported range")};
            return false;
        }
        bool bActorReplicates = false;
        bool bMovement = false;
        bool bAlways = false;
        bool bOwnerOnly = false;
        ReadBoolProperty(Defaults, TEXT("bReplicates"), bActorReplicates);
        ReadBoolProperty(Defaults, TEXT("bReplicateMovement"), bMovement);
        ReadBoolProperty(Defaults, TEXT("bAlwaysRelevant"), bAlways);
        ReadBoolProperty(Defaults, TEXT("bOnlyRelevantToOwner"), bOwnerOnly);
        if (PropertyName == TEXT("bReplicateMovement") && bValue && !bActorReplicates)
        {
            OutError = {TEXT("invalid_argument"), TEXT("Actor replication must be enabled before movement replication")};
            return false;
        }
        if (PropertyName == TEXT("bReplicates") && !bValue && bMovement)
        {
            OutError = {TEXT("invalid_argument"), TEXT("Disable movement replication before Actor replication")};
            return false;
        }
        if (PropertyName == TEXT("bReplicates") && !bValue && Blueprint->SimpleConstructionScript != nullptr)
        {
            for (USCS_Node* Candidate : Blueprint->SimpleConstructionScript->GetAllNodes())
            {
                const UActorComponent* Component = Candidate != nullptr ? Cast<UActorComponent>(Candidate->ComponentTemplate) : nullptr;
                if (Component != nullptr && Component->GetIsReplicated())
                {
                    OutError = {TEXT("invalid_argument"), TEXT("Disable local component replication before Actor replication")};
                    return false;
                }
            }
        }
        if ((PropertyName == TEXT("bAlwaysRelevant") && bValue && bOwnerOnly)
            || (PropertyName == TEXT("bOnlyRelevantToOwner") && bValue && bAlways))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Always-relevant and owner-only relevancy cannot both be enabled")};
            return false;
        }
        double Update = 0.0;
        double Minimum = 0.0;
        ReadNumberProperty(Defaults, TEXT("NetUpdateFrequency"), Update);
        ReadNumberProperty(Defaults, TEXT("MinNetUpdateFrequency"), Minimum);
        if ((PropertyName == TEXT("NetUpdateFrequency") && Number < Minimum)
            || (PropertyName == TEXT("MinNetUpdateFrequency") && Number > Update))
        {
            OutError = {TEXT("invalid_argument"), TEXT("Minimum net update frequency cannot exceed net update frequency")};
            return false;
        }
    }
    else if (ReplicationProperties.FindKey(PropertyName) != nullptr)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Use replication_setting for bounded Actor replication defaults")};
        return false;
    }
    TSharedPtr<FJsonObject> Changed;
    bool bApplied = false;
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP Blueprint default edit")));
        Blueprint->Modify();
        Defaults->SetFlags(RF_Transactional);
        Defaults->Modify();
        bApplied = UnrealMCP::PropertyCodec::Set(
            Defaults, PropertyName, Arguments->Values.FindRef(TEXT("value")), Changed, OutError);
        if (bApplied)
        {
            FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
            Defaults = Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
            FProperty* LiveProperty = Defaults != nullptr ? Defaults->GetClass()->FindPropertyByName(FName(*PropertyName)) : nullptr;
            bApplied = Defaults != nullptr && LiveProperty != nullptr;
            if (bApplied) Changed = UnrealMCP::PropertyCodec::Encode(Defaults, LiveProperty);
        }
    }
    if (!bApplied)
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    FString Snapshot;
    if (!ReadSnapshot(Inspector, ObjectPath, Snapshot, OutError))
    {
        RestoreFailedTransaction(OutError);
        return false;
    }
    OutResult = BuildEditResult(Blueprint, ObjectPath, Snapshot, TEXT("set_property"), Changed);
    return true;
}
