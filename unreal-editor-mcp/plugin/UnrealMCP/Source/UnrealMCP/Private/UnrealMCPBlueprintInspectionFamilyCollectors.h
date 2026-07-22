#pragma once

#include "UnrealMCPBlueprintInspectionSupport.h"

namespace UnrealMCP::BlueprintInspectionPrivate
{
static bool CollectOverviewAndComponents(
    UBlueprint* Blueprint,
    const FString& AssetPath,
    bool bWasLoaded,
    bool bDirtyBefore,
    bool bIncludeInherited,
    const FString& ComponentFilter,
    const TSet<FString>& PropertyNames,
    const TSet<FString>& Sections,
    FInspectionSink& Sink,
    TArray<TPair<UBlueprint*, FString>>& Owners,
    FUnrealMCPError& OutError)
{
if (Sections.Contains(TEXT("summary")))
{
    const TSharedRef<FJsonObject> Value = Record(TEXT("summary"));
    Value->SetStringField(TEXT("asset_path"), AssetPath);
    Value->SetStringField(TEXT("asset_name"), Blueprint->GetName());
    Value->SetBoolField(TEXT("was_loaded"), bWasLoaded);
    Value->SetBoolField(TEXT("package_dirty"), bDirtyBefore);
    Value->SetBoolField(TEXT("actor_blueprint"),
        Blueprint->ParentClass != nullptr && Blueprint->ParentClass->IsChildOf(AActor::StaticClass()));
    Value->SetStringField(TEXT("blueprint_family"), UnrealMCP::BlueprintFamilyPolicy::Classify(Blueprint->ParentClass).Name);
    Value->SetObjectField(TEXT("family_capabilities"), UnrealMCP::BlueprintFamilyPolicy::BuildLiveCapabilities(Blueprint));
    AddRecord(Sink.Records, Value);
}
if (Sections.Contains(TEXT("parent_class")))
{
    const TSharedRef<FJsonObject> Value = Record(TEXT("parent_class"));
    Value->SetStringField(TEXT("class_path"), Blueprint->ParentClass->GetPathName());
    Value->SetBoolField(TEXT("blueprint_generated"), UBlueprint::GetBlueprintFromClass(Blueprint->ParentClass) != nullptr);
    AddRecord(Sink.Records, Value);
}
if (Sections.Contains(TEXT("compile_state")))
{
    const TSharedRef<FJsonObject> Value = Record(TEXT("compile_state"));
    Value->SetStringField(TEXT("state"), CompileState(Blueprint->Status));
    Value->SetBoolField(TEXT("being_compiled"), Blueprint->bBeingCompiled != 0);
    AddRecord(Sink.Records, Value);
}

Owners.Emplace(Blueprint, AssetPath);
if (bIncludeInherited)
{
    for (UClass* Class = Blueprint->ParentClass; Class != nullptr; Class = Class->GetSuperClass())
    {
        UBlueprint* ParentBlueprint = UBlueprint::GetBlueprintFromClass(Class);
        if (ParentBlueprint != nullptr && !Owners.ContainsByPredicate([ParentBlueprint](const TPair<UBlueprint*, FString>& Pair) { return Pair.Key == ParentBlueprint; }))
        {
            Owners.Emplace(ParentBlueprint, ParentBlueprint->GetPathName());
        }
    }
}

bool bComponentFound = ComponentFilter.IsEmpty();
for (const TPair<UBlueprint*, FString>& Owner : Owners)
{
    if (Owner.Key->SimpleConstructionScript == nullptr) continue;
    TMap<const USCS_Node*, const USCS_Node*> Parents;
    for (USCS_Node* Node : Owner.Key->SimpleConstructionScript->GetAllNodes())
    {
        if (Node == nullptr) continue;
        for (USCS_Node* Child : Node->GetChildNodes()) Parents.Add(Child, Node);
    }
    for (USCS_Node* Node : Owner.Key->SimpleConstructionScript->GetAllNodes())
    {
        if (Node == nullptr) continue;
        const TSharedRef<FJsonObject> Value = Record(TEXT("component"));
        const FString Id = GuidString(Node->VariableGuid);
        if (Id == ComponentFilter) bComponentFound = true;
        const USCS_Node* const* Parent = Parents.Find(Node);
        FString DefaultsFingerprint;
        if (Node->ComponentTemplate != nullptr) DefaultsFingerprint = AddComponentDefaults(Node->ComponentTemplate, PropertyNames, Value);
        if (Sections.Contains(TEXT("components")) && (ComponentFilter.IsEmpty() || ComponentFilter == Id))
        {
            Value->SetStringField(TEXT("id"), Id);
            Value->SetBoolField(TEXT("identity_stable"), !Id.IsEmpty());
            Value->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
            Value->SetStringField(TEXT("class_path"), Node->ComponentClass != nullptr ? Node->ComponentClass->GetPathName() : FString());
            Value->SetStringField(TEXT("owner_blueprint"), Owner.Value);
            Value->SetBoolField(TEXT("inherited"), Owner.Key != Blueprint);
            Value->SetStringField(TEXT("ownership"), Owner.Key == Blueprint ? TEXT("local") : TEXT("inherited"));
            Value->SetBoolField(TEXT("native"), false);
            Value->SetBoolField(TEXT("instanced"), false);
            Value->SetBoolField(TEXT("editable"), Owner.Key == Blueprint && !Id.IsEmpty());
            Value->SetBoolField(TEXT("scene_component"), Node->ComponentClass != nullptr && Node->ComponentClass->IsChildOf(USceneComponent::StaticClass()));
            Value->SetBoolField(TEXT("root"), Owner.Key->SimpleConstructionScript->GetRootNodes().Contains(Node));
            Value->SetStringField(TEXT("parent_id"), Parent != nullptr && *Parent != nullptr ? GuidString((*Parent)->VariableGuid) : FString());
            AddRecord(Sink.Records, Value);
        }
        Sink.Fingerprint.Add(TEXT("component|") + Owner.Value + TEXT("|") + Id + TEXT("|") + Node->GetVariableName().ToString()
            + TEXT("|") + (Parent != nullptr && *Parent != nullptr ? GuidString((*Parent)->VariableGuid) : FString()) + TEXT("|") + DefaultsFingerprint);
    }
}
if (!bComponentFound)
{
    OutError = {TEXT("not_found"), TEXT("The requested component identity was not found")};
    return false;
}

if (bIncludeInherited && Blueprint->GeneratedClass != nullptr)
{
    if (AActor* DefaultsActor = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(false)))
    {
        TInlineComponentArray<UActorComponent*> NativeComponents;
        DefaultsActor->GetComponents(NativeComponents);
        NativeComponents.Sort([](const UActorComponent& Left, const UActorComponent& Right)
        {
            return Left.GetName() < Right.GetName();
        });
        for (UActorComponent* Component : NativeComponents)
        {
            if (Component == nullptr || Component->CreationMethod != EComponentCreationMethod::Native) continue;
            const TSharedRef<FJsonObject> Value = Record(TEXT("component"));
            const FString DefaultsFingerprint = AddComponentDefaults(Component, PropertyNames, Value);
            if (Sections.Contains(TEXT("components")) && ComponentFilter.IsEmpty())
            {
                Value->SetStringField(TEXT("id"), FString());
                Value->SetBoolField(TEXT("identity_stable"), false);
                Value->SetStringField(TEXT("name"), Component->GetName());
                Value->SetStringField(TEXT("class_path"), Component->GetClass()->GetPathName());
                Value->SetStringField(TEXT("owner_blueprint"), Blueprint->ParentClass != nullptr ? Blueprint->ParentClass->GetPathName() : FString());
                Value->SetBoolField(TEXT("inherited"), true);
                Value->SetStringField(TEXT("ownership"), TEXT("native"));
                Value->SetBoolField(TEXT("native"), true);
                Value->SetBoolField(TEXT("instanced"), false);
                Value->SetBoolField(TEXT("editable"), false);
                Value->SetBoolField(TEXT("scene_component"), Component->IsA<USceneComponent>());
                Value->SetBoolField(TEXT("root"), DefaultsActor->GetRootComponent() == Component);
                Value->SetStringField(TEXT("parent_id"), FString());
                AddRecord(Sink.Records, Value);
            }
            Sink.Fingerprint.Add(TEXT("native_component|") + Component->GetName() + TEXT("|")
                + Component->GetClass()->GetPathName() + TEXT("|") + DefaultsFingerprint);
        }
    }
}

if (Sections.Contains(TEXT("class_defaults")))
{
    UObject* Defaults = Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
    if (Defaults == nullptr)
    {
        OutError = {TEXT("busy"), TEXT("The Blueprint generated-class defaults are unavailable"), MakeShared<FJsonObject>(), true};
        return false;
    }
    TArray<FString> SortedNames = PropertyNames.Array();
    SortedNames.Sort();
    for (const FString& Name : SortedNames)
    {
        const TSharedRef<FJsonObject> Value = Record(TEXT("class_default"));
        const TSharedRef<FJsonObject> Encoded = UnrealMCP::PropertyCodec::Encode(
            Defaults, Defaults->GetClass()->FindPropertyByName(FName(*Name)));
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Encoded->Values) Value->SetField(Pair.Key, Pair.Value);
        AddRecord(Sink.Records, Value);
    }
}
    return true;
}

static bool CollectMembers(
    UBlueprint* Blueprint,
    const TArray<TPair<UBlueprint*, FString>>& Owners,
    const TSet<FString>& Sections,
    const FString& MemberFilter,
    FInspectionSink& Sink,
    FUnrealMCPError& OutError)
{
bool bMemberFound = MemberFilter.IsEmpty();
for (const TPair<UBlueprint*, FString>& Owner : Owners)
{
    for (const FBPVariableDescription& Variable : Owner.Key->NewVariables)
    {
        const FString Id = GuidString(Variable.VarGuid);
        const bool bSelected = MemberFilter.IsEmpty() || Id == MemberFilter;
        if (bSelected) bMemberFound = true;
        const FString EffectiveDefault = VariableDefaultText(Owner.Key, Variable);
        if (Sections.Contains(TEXT("variables")) && bSelected)
        {
            const TSharedRef<FJsonObject> Value = Record(TEXT("variable"));
            Value->SetStringField(TEXT("id"), Id);
            Value->SetBoolField(TEXT("identity_stable"), !Id.IsEmpty());
            Value->SetStringField(TEXT("name"), Variable.VarName.ToString());
            Value->SetStringField(TEXT("owner_blueprint"), Owner.Value);
            Value->SetBoolField(TEXT("inherited"), Owner.Key != Blueprint);
            Value->SetStringField(TEXT("ownership"), Owner.Key == Blueprint ? TEXT("local") : TEXT("inherited"));
            Value->SetBoolField(TEXT("editable"), Owner.Key == Blueprint && !Id.IsEmpty());
            Value->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(Variable.VarType));
            Value->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(Variable.VarType, EffectiveDefault));
            Value->SetObjectField(TEXT("metadata"), VariableMetadata(Variable));
            Value->SetObjectField(TEXT("replication"), VariableReplication(Owner.Key, Variable, Owner.Key == Blueprint && !Id.IsEmpty()));
            Value->SetObjectField(TEXT("reference_summary"), VariableReferences(Blueprint, Variable.VarName));
            AddRecord(Sink.Records, Value);
        }
        TArray<FString> MetadataFingerprint;
        for (const FBPVariableMetaDataEntry& Entry : Variable.MetaDataArray)
        {
            MetadataFingerprint.Add(Entry.DataKey.ToString() + TEXT("=") + Entry.DataValue);
        }
        MetadataFingerprint.Sort();
        Sink.Fingerprint.Add(TEXT("variable|") + Owner.Value + TEXT("|") + GuidString(Variable.VarGuid) + TEXT("|") + Variable.VarName.ToString()
            + TEXT("|") + VariableTypeFingerprint(Variable.VarType) + TEXT("|") + EffectiveDefault
            + TEXT("|") + Variable.Category.ToString() + TEXT("|") + LexToString(Variable.PropertyFlags)
            + TEXT("|") + Variable.RepNotifyFunc.ToString() + TEXT("|") + LexToString(static_cast<int32>(Variable.ReplicationCondition))
            + TEXT("|") + FString::Join(MetadataFingerprint, TEXT(";")));
    }
}
if (!bMemberFound)
{
    OutError = {TEXT("not_found"), TEXT("The requested member identity was not found")};
    return false;
}
    return true;
}

static bool CollectFunctionsAndLocals(
    UBlueprint* Blueprint,
    const TArray<TPair<UBlueprint*, FString>>& Owners,
    const TSet<FString>& Sections,
    const FString& FunctionFilter,
    const FString& LocalFilter,
    const FString& MacroFilter,
    const FString& CustomEventFilter,
    FInspectionSink& Sink,
    FUnrealMCPError& OutError)
{
bool bFunctionFound = FunctionFilter.IsEmpty();
bool bLocalFound = LocalFilter.IsEmpty();
for (const TPair<UBlueprint*, FString>& Owner : Owners)
{
    for (UEdGraph* FunctionGraph : Owner.Key->FunctionGraphs)
    {
        if (FunctionGraph == nullptr) continue;
        const FString FunctionId = GuidString(FunctionGraph->GraphGuid);
        if (!FunctionFilter.IsEmpty() && FunctionId != FunctionFilter) continue;
        bFunctionFound = true;
        UK2Node_FunctionEntry* Entry = FunctionEntry(FunctionGraph);
        TArray<UK2Node_FunctionResult*> Results;
        FunctionGraph->GetNodesOfClass(Results);
        Results.Sort([](const UK2Node_FunctionResult& Left, const UK2Node_FunctionResult& Right)
        {
            return GuidString(Left.NodeGuid) < GuidString(Right.NodeGuid);
        });
        const FString DeclarationKind = FunctionOwnership(Owner.Key, FunctionGraph);
        const bool bLocalOwner = Owner.Key == Blueprint;
        const bool bEditable = bLocalOwner && DeclarationKind == TEXT("user_owned") && Entry != nullptr && FunctionId.Len() == 32;
        if (Entry == nullptr)
        {
            Sink.Fingerprint.Add(TEXT("function|missing_entry|") + Owner.Value + TEXT("|") + FunctionId + TEXT("|") + FunctionGraph->GetName());
            continue;
        }
        const TSharedRef<FJsonObject> Signature = FunctionSignature(Entry, Results);
        const TSharedRef<FJsonObject> References = FunctionReferences(Blueprint, FunctionGraph);
        TArray<TSharedPtr<FJsonValue>> RepNotifyMembers;
        for (const FBPVariableDescription& Variable : Owner.Key->NewVariables)
        {
            if (Variable.RepNotifyFunc == FunctionGraph->GetFName())
            {
                RepNotifyMembers.Add(MakeShared<FJsonValueString>(GuidString(Variable.VarGuid)));
            }
        }
        if (Sections.Contains(TEXT("functions")))
        {
            const TSharedRef<FJsonObject> Value = Record(TEXT("function"));
            Value->SetStringField(TEXT("id"), FunctionId);
            Value->SetBoolField(TEXT("identity_stable"), !FunctionId.IsEmpty());
            Value->SetStringField(TEXT("name"), FunctionGraph->GetName());
            Value->SetStringField(TEXT("owner_blueprint"), Owner.Value);
            Value->SetBoolField(TEXT("inherited"), !bLocalOwner);
            Value->SetStringField(TEXT("ownership"), bLocalOwner ? DeclarationKind : TEXT("inherited"));
            Value->SetBoolField(TEXT("editable"), bEditable);
            Value->SetObjectField(TEXT("signature"), Signature);
            Value->SetObjectField(TEXT("metadata"), FunctionMetadata(Entry));
            Value->SetObjectField(TEXT("reference_summary"), References);
            const TSharedRef<FJsonObject> Required = MakeShared<FJsonObject>();
            Required->SetStringField(TEXT("entry_node_id"), GuidString(Entry->NodeGuid));
            Required->SetBoolField(TEXT("entry_present"), true);
            Required->SetNumberField(TEXT("result_count"), Results.Num());
            Required->SetStringField(TEXT("result_node_id"), !Results.IsEmpty() ? GuidString(Results[0]->NodeGuid) : FString());
            Required->SetBoolField(TEXT("result_present"), !Results.IsEmpty());
            Required->SetBoolField(TEXT("valid"), !Results.IsEmpty());
            Value->SetObjectField(TEXT("required_nodes"), Required);
            Value->SetArrayField(TEXT("rep_notify_member_ids"), RepNotifyMembers);
            Value->SetNumberField(TEXT("local_variable_count"), Entry->LocalVariables.Num());
            AddRecord(Sink.Records, Value);
        }

        int32 ParameterIndex = 0;
        auto AppendParameters = [&](UK2Node_EditablePinBase* Node, const TCHAR* Direction)
        {
            if (Node == nullptr) return;
            for (const TSharedPtr<FUserPinInfo>& Pin : Node->UserDefinedPins)
            {
                if (!Pin.IsValid()) continue;
                UEdGraphPin* LivePin = Node->FindPin(Pin->PinName);
                if (Sections.Contains(TEXT("parameters")) && MacroFilter.IsEmpty() && CustomEventFilter.IsEmpty())
                {
                    const TSharedRef<FJsonObject> Value = Record(TEXT("parameter"));
                    Value->SetStringField(TEXT("id"), LivePin != nullptr ? GuidString(LivePin->PinId) : FString());
                    Value->SetBoolField(TEXT("identity_stable"), LivePin != nullptr && LivePin->PinId.IsValid());
                    Value->SetStringField(TEXT("function_id"), FunctionId);
                    Value->SetStringField(TEXT("function_name"), FunctionGraph->GetName());
                    Value->SetNumberField(TEXT("index"), ParameterIndex);
                    Value->SetStringField(TEXT("name"), Pin->PinName.ToString());
                    Value->SetStringField(TEXT("direction"), Direction);
                    Value->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(Pin->PinType));
                    if (FCString::Strcmp(Direction, TEXT("input")) == 0 && !Pin->PinType.bIsReference)
                    {
                        Value->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(Pin->PinType, Pin->PinDefaultValue));
                    }
                    AddRecord(Sink.Records, Value);
                }
                Sink.Fingerprint.Add(TEXT("parameter|") + FunctionId + TEXT("|") + LexToString(ParameterIndex) + TEXT("|")
                    + Direction + TEXT("|") + Pin->PinName.ToString() + TEXT("|") + VariableTypeFingerprint(Pin->PinType)
                    + TEXT("|") + LexToString(Pin->PinType.bIsReference) + TEXT("|") + LexToString(Pin->PinType.bIsConst)
                    + TEXT("|") + Pin->PinDefaultValue);
                ++ParameterIndex;
            }
        };
        AppendParameters(Entry, TEXT("input"));
        if (!Results.IsEmpty()) AppendParameters(Results[0], TEXT("output"));

        for (const FBPVariableDescription& Local : Entry->LocalVariables)
        {
            const FString LocalId = GuidString(Local.VarGuid);
            if (!LocalFilter.IsEmpty() && LocalId != LocalFilter) continue;
            bLocalFound = true;
            const TSharedRef<FJsonObject> LocalReferenceSummary = LocalReferences(Blueprint, FunctionGraph, Local.VarName);
            if (Sections.Contains(TEXT("local_variables")))
            {
                const TSharedRef<FJsonObject> Value = Record(TEXT("local_variable"));
                Value->SetStringField(TEXT("id"), LocalId);
                Value->SetBoolField(TEXT("identity_stable"), !LocalId.IsEmpty());
                Value->SetStringField(TEXT("name"), Local.VarName.ToString());
                Value->SetStringField(TEXT("owner_blueprint"), Owner.Value);
                Value->SetBoolField(TEXT("inherited"), !bLocalOwner);
                Value->SetStringField(TEXT("ownership"), bLocalOwner ? TEXT("local") : TEXT("inherited"));
                Value->SetBoolField(TEXT("editable"), bEditable && !LocalId.IsEmpty());
                const TSharedRef<FJsonObject> Scope = MakeShared<FJsonObject>();
                Scope->SetStringField(TEXT("function_id"), FunctionId);
                Scope->SetStringField(TEXT("function_name"), FunctionGraph->GetName());
                Value->SetObjectField(TEXT("scope"), Scope);
                Value->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(Local.VarType));
                Value->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(Local.VarType, Local.DefaultValue));
                Value->SetObjectField(TEXT("reference_summary"), LocalReferenceSummary);
                AddRecord(Sink.Records, Value);
            }
            Sink.Fingerprint.Add(TEXT("local|") + FunctionId + TEXT("|") + LocalId + TEXT("|") + Local.VarName.ToString()
                + TEXT("|") + VariableTypeFingerprint(Local.VarType) + TEXT("|") + Local.DefaultValue);
        }
        Sink.Fingerprint.Add(TEXT("function|") + Owner.Value + TEXT("|") + FunctionId + TEXT("|") + FunctionGraph->GetName()
            + TEXT("|") + DeclarationKind + TEXT("|") + LexToString(Entry->GetFunctionFlags())
            + TEXT("|") + Entry->MetaData.Category.ToString() + TEXT("|") + Entry->MetaData.ToolTip.ToString()
            + TEXT("|") + Entry->MetaData.Keywords.ToString() + TEXT("|") + LexToString(Entry->MetaData.bCallInEditor)
            + TEXT("|") + LexToString(Results.Num()));
    }
}
if (!bFunctionFound)
{
    OutError = {TEXT("not_found"), TEXT("The requested function identity was not found")};
    return false;
}
if (!bLocalFound)
{
    OutError = {TEXT("not_found"), TEXT("The requested local-variable identity was not found")};
    return false;
}
    return true;
}

static bool CollectMacros(
    UBlueprint* Blueprint,
    const TArray<TPair<UBlueprint*, FString>>& Owners,
    const TSet<FString>& Sections,
    const FString& FunctionFilter,
    const FString& LocalFilter,
    const FString& CustomEventFilter,
    const FString& MacroFilter,
    FInspectionSink& Sink,
    FUnrealMCPError& OutError)
{
bool bMacroFound = MacroFilter.IsEmpty();
for (const TPair<UBlueprint*, FString>& Owner : Owners)
{
    for (UEdGraph* MacroGraph : Owner.Key->MacroGraphs)
    {
        if (MacroGraph == nullptr) continue;
        const FString MacroId = GuidString(MacroGraph->GraphGuid);
        if (!MacroFilter.IsEmpty() && MacroId != MacroFilter) continue;
        bMacroFound = true;
        UK2Node_Tunnel* Entry = nullptr;
        UK2Node_Tunnel* Exit = nullptr;
        bool bPure = false;
        FKismetEditorUtilities::GetInformationOnMacro(MacroGraph, Entry, Exit, bPure);
        const bool bLocalOwner = Owner.Key == Blueprint;
        const bool bEditable = bLocalOwner && MacroId.Len() == 32 && Entry != nullptr && Exit != nullptr;
        const TSharedRef<FJsonObject> Signature = MacroSignature(Entry, Exit, bPure);
        const TSharedRef<FJsonObject> References = MacroReferences(Blueprint, MacroGraph);
        if (Sections.Contains(TEXT("macros")))
        {
            const TSharedRef<FJsonObject> Value = Record(TEXT("macro"));
            Value->SetStringField(TEXT("id"), MacroId);
            Value->SetBoolField(TEXT("identity_stable"), !MacroId.IsEmpty());
            Value->SetStringField(TEXT("name"), MacroGraph->GetName());
            Value->SetStringField(TEXT("owner_blueprint"), Owner.Value);
            Value->SetBoolField(TEXT("inherited"), !bLocalOwner);
            Value->SetStringField(TEXT("ownership"), bLocalOwner ? TEXT("local") : TEXT("inherited"));
            Value->SetBoolField(TEXT("editable"), bEditable);
            Value->SetObjectField(TEXT("signature"), Signature);
            Value->SetObjectField(TEXT("metadata"), Entry != nullptr
                ? CallableMetadata(Entry->MetaData, false) : CallableMetadata(FKismetUserDeclaredFunctionMetadata(), false));
            Value->SetObjectField(TEXT("reference_summary"), References);
            const TSharedRef<FJsonObject> Relationship = MakeShared<FJsonObject>();
            Relationship->SetStringField(TEXT("graph_id"), MacroId);
            Relationship->SetStringField(TEXT("graph_kind"), TEXT("macro"));
            Value->SetObjectField(TEXT("graph_relationship"), Relationship);
            const TSharedRef<FJsonObject> Required = MakeShared<FJsonObject>();
            Required->SetStringField(TEXT("entry_node_id"), Entry != nullptr ? GuidString(Entry->NodeGuid) : FString());
            Required->SetStringField(TEXT("exit_node_id"), Exit != nullptr ? GuidString(Exit->NodeGuid) : FString());
            Required->SetBoolField(TEXT("entry_present"), Entry != nullptr);
            Required->SetBoolField(TEXT("exit_present"), Exit != nullptr);
            Required->SetBoolField(TEXT("valid"), Entry != nullptr && Exit != nullptr);
            Value->SetObjectField(TEXT("required_nodes"), Required);
            AddRecord(Sink.Records, Value);
        }
        int32 ParameterIndex = 0;
        auto AppendMacroParameters = [&](UK2Node_Tunnel* Node, const TCHAR* Direction)
        {
            if (Node == nullptr) return;
            for (const TSharedPtr<FUserPinInfo>& Pin : Node->UserDefinedPins)
            {
                if (!Pin.IsValid() || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
                UEdGraphPin* LivePin = Node->FindPin(Pin->PinName);
                if (Sections.Contains(TEXT("parameters")) && FunctionFilter.IsEmpty()
                    && LocalFilter.IsEmpty() && CustomEventFilter.IsEmpty())
                {
                    const TSharedRef<FJsonObject> Value = Record(TEXT("parameter"));
                    Value->SetStringField(TEXT("id"), LivePin != nullptr ? GuidString(LivePin->PinId) : FString());
                    Value->SetBoolField(TEXT("identity_stable"), LivePin != nullptr && LivePin->PinId.IsValid());
                    Value->SetStringField(TEXT("owner_kind"), TEXT("macro"));
                    Value->SetStringField(TEXT("owner_id"), MacroId);
                    Value->SetStringField(TEXT("macro_id"), MacroId);
                    Value->SetStringField(TEXT("macro_name"), MacroGraph->GetName());
                    Value->SetNumberField(TEXT("index"), ParameterIndex);
                    Value->SetStringField(TEXT("name"), Pin->PinName.ToString());
                    Value->SetStringField(TEXT("direction"), Direction);
                    Value->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(Pin->PinType));
                    if (FCString::Strcmp(Direction, TEXT("input")) == 0 && !Pin->PinType.bIsReference)
                    {
                        Value->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(Pin->PinType, Pin->PinDefaultValue));
                    }
                    AddRecord(Sink.Records, Value);
                }
                Sink.Fingerprint.Add(TEXT("macro_parameter|") + MacroId + TEXT("|") + LexToString(ParameterIndex) + TEXT("|")
                    + Direction + TEXT("|") + Pin->PinName.ToString() + TEXT("|") + VariableTypeFingerprint(Pin->PinType)
                    + TEXT("|") + Pin->PinDefaultValue);
                ++ParameterIndex;
            }
        };
        AppendMacroParameters(Entry, TEXT("input"));
        AppendMacroParameters(Exit, TEXT("output"));
        const FKismetUserDeclaredFunctionMetadata* Metadata = Entry != nullptr ? &Entry->MetaData : nullptr;
        Sink.Fingerprint.Add(TEXT("macro|") + Owner.Value + TEXT("|") + MacroId + TEXT("|") + MacroGraph->GetName()
            + TEXT("|") + LexToString(bPure) + TEXT("|") + (Metadata != nullptr ? Metadata->Category.ToString() : FString())
            + TEXT("|") + (Metadata != nullptr ? Metadata->ToolTip.ToString() : FString())
            + TEXT("|") + (Metadata != nullptr ? Metadata->Keywords.ToString() : FString())
            + TEXT("|") + (Entry != nullptr ? GuidString(Entry->NodeGuid) : FString())
            + TEXT("|") + (Exit != nullptr ? GuidString(Exit->NodeGuid) : FString()));
    }
}
if (!bMacroFound)
{
    OutError = {TEXT("not_found"), TEXT("The requested macro identity was not found")};
    return false;
}
    return true;
}
}
