#include "UnrealMCPBlueprintInspector.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Blueprint/BlueprintSupport.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "K2Node.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMCPVersion.h"
#include "UnrealMCPK2TypeCodec.h"
#include "UnrealMCPPropertyCodec.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace
{
const TSet<FString> InspectSections = {
    TEXT("summary"), TEXT("parent_class"), TEXT("compile_state"), TEXT("components"),
    TEXT("class_defaults"), TEXT("variables"), TEXT("functions"), TEXT("parameters"), TEXT("local_variables"),
    TEXT("graphs"), TEXT("nodes"), TEXT("pins"), TEXT("connections")};

const TSet<FString> SupportedPinCategories = {
    TEXT("exec"), TEXT("boolean"), TEXT("byte"), TEXT("int"), TEXT("int64"), TEXT("real"),
    TEXT("float"), TEXT("double"), TEXT("name"), TEXT("string"), TEXT("text"), TEXT("enum"),
    TEXT("struct"), TEXT("object"), TEXT("class"), TEXT("softobject"), TEXT("softclass"),
    TEXT("interface"), TEXT("wildcard")};

FString GuidString(const FGuid& Guid)
{
    return Guid.IsValid() ? Guid.ToString(EGuidFormats::Digits).ToLower() : FString();
}

FString HashLines(TArray<FString> Lines)
{
    Lines.Sort();
    const FString Joined = FString::Join(Lines, TEXT("\n"));
    FTCHARToUTF8 Encoded(*Joined);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

TSharedRef<FJsonObject> Record(const TCHAR* Section)
{
    const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
    Value->SetStringField(TEXT("section"), Section);
    return Value;
}

void AddRecord(TArray<TSharedPtr<FJsonValue>>& Records, const TSharedRef<FJsonObject>& Value)
{
    Records.Add(MakeShared<FJsonValueObject>(Value));
}

bool HasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed)
{
    TSet<FString> Names;
    for (const TCHAR* Name : Allowed)
    {
        Names.Add(Name);
    }
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
    {
        if (!Names.Contains(Pair.Key))
        {
            return false;
        }
    }
    return true;
}

bool ReadPageSize(const FJsonObject& Object, int32& OutPageSize, FUnrealMCPError& OutError)
{
    OutPageSize = UnrealMCP::DefaultInspectPageSize;
    if (!Object.HasField(TEXT("page_size")))
    {
        return true;
    }
    double Number = 0.0;
    if (!Object.TryGetNumberField(TEXT("page_size"), Number) || !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number)))
    {
        OutError = {TEXT("invalid_argument"), TEXT("page_size must be an integer")};
        return false;
    }
    OutPageSize = static_cast<int32>(Number);
    if (OutPageSize < 1 || OutPageSize > UnrealMCP::MaxInspectPageSize)
    {
        OutError = {TEXT("invalid_argument"), TEXT("page_size is outside the supported range")};
        return false;
    }
    return true;
}

bool ReadOptionalBool(const FJsonObject& Object, const TCHAR* Name, bool DefaultValue, bool& OutValue, FUnrealMCPError& OutError)
{
    OutValue = DefaultValue;
    if (!Object.HasField(Name))
    {
        return true;
    }
    if (!Object.TryGetBoolField(Name, OutValue))
    {
        OutError = {TEXT("invalid_argument"), FString::Printf(TEXT("%s must be boolean"), Name)};
        return false;
    }
    return true;
}

bool NormalizePackagePath(const FString& Input, FString& OutPath)
{
    OutPath = Input;
    while (OutPath.Len() > 1 && OutPath.EndsWith(TEXT("/")))
    {
        OutPath.LeftChopInline(1);
    }
    return OutPath.IsEmpty()
        || (OutPath.StartsWith(TEXT("/")) && !OutPath.StartsWith(TEXT("//")) && !OutPath.Contains(TEXT(".."))
            && !OutPath.Contains(TEXT("\\")) && FPackageName::IsValidLongPackageName(OutPath, true));
}

bool NormalizeAssetPath(const FString& Input, FString& OutObjectPath)
{
    if (!Input.StartsWith(TEXT("/")) || Input.StartsWith(TEXT("//")) || Input.Contains(TEXT("..")) || Input.Contains(TEXT("\\")))
    {
        return false;
    }
    FString PackageName = FPackageName::ObjectPathToPackageName(Input);
    if (!FPackageName::IsValidLongPackageName(PackageName, true))
    {
        return false;
    }
    if (Input.Contains(TEXT(".")))
    {
        if (!FPackageName::IsValidObjectPath(Input))
        {
            return false;
        }
        OutObjectPath = Input;
        return true;
    }
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    OutObjectPath = PackageName + TEXT(".") + AssetName;
    return FPackageName::IsValidObjectPath(OutObjectPath);
}

FString CompileState(EBlueprintStatus Status)
{
    switch (Status)
    {
    case BS_Dirty: return TEXT("dirty");
    case BS_Error: return TEXT("error");
    case BS_UpToDate: return TEXT("up_to_date");
    case BS_BeingCreated: return TEXT("being_created");
    case BS_UpToDateWithWarnings: return TEXT("up_to_date_with_warnings");
    default: return TEXT("unknown");
    }
}

FString ContainerName(EPinContainerType Type)
{
    switch (Type)
    {
    case EPinContainerType::Array: return TEXT("array");
    case EPinContainerType::Set: return TEXT("set");
    case EPinContainerType::Map: return TEXT("map");
    default: return TEXT("none");
    }
}

FString VariableTypeFingerprint(const FEdGraphPinType& Type)
{
    const UObject* KeyTypeObject = Type.PinSubCategoryObject.Get();
    const UObject* ValueTypeObject = Type.PinValueType.TerminalSubCategoryObject.Get();
    return Type.PinCategory.ToString() + TEXT("|") + Type.PinSubCategory.ToString() + TEXT("|")
        + (KeyTypeObject != nullptr ? KeyTypeObject->GetPathName() : FString()) + TEXT("|")
        + ContainerName(Type.ContainerType) + TEXT("|") + Type.PinValueType.TerminalCategory.ToString() + TEXT("|")
        + Type.PinValueType.TerminalSubCategory.ToString() + TEXT("|")
        + (ValueTypeObject != nullptr ? ValueTypeObject->GetPathName() : FString());
}

TSharedRef<FJsonObject> PinType(const FEdGraphPinType& Type)
{
    const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
    const FString Category = Type.PinCategory.ToString().ToLower();
    Value->SetStringField(TEXT("category"), Category);
    Value->SetStringField(TEXT("subcategory"), Type.PinSubCategory.ToString());
    Value->SetStringField(TEXT("container"), ContainerName(Type.ContainerType));
    Value->SetBoolField(TEXT("reference"), Type.bIsReference);
    Value->SetBoolField(TEXT("const"), Type.bIsConst);
    Value->SetBoolField(TEXT("supported"), SupportedPinCategories.Contains(Category));
    if (const UObject* TypeObject = Type.PinSubCategoryObject.Get())
    {
        Value->SetStringField(TEXT("type_object"), TypeObject->GetPathName());
    }
    return Value;
}

TSharedRef<FJsonObject> VariableReferences(UBlueprint* Blueprint, const FName VariableName)
{
    TArray<UK2Node*> Nodes;
    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);
    for (UEdGraph* Graph : AllGraphs)
    {
        if (Graph == nullptr) continue;
        for (UEdGraphNode* GraphNode : Graph->Nodes)
        {
            UK2Node* Node = Cast<UK2Node>(GraphNode);
            if (Node != nullptr && Node->ReferencesVariable(VariableName, nullptr)) Nodes.Add(Node);
        }
    }
    const bool bReferenced = !Nodes.IsEmpty() || FBlueprintEditorUtils::IsVariableUsed(Blueprint, VariableName);
    Nodes.Sort([](const UK2Node& Left, const UK2Node& Right)
    {
        const UEdGraph* LeftGraph = Left.GetGraph();
        const UEdGraph* RightGraph = Right.GetGraph();
        return (LeftGraph != nullptr ? GuidString(LeftGraph->GraphGuid) : FString()) + GuidString(Left.NodeGuid)
            < (RightGraph != nullptr ? GuidString(RightGraph->GraphGuid) : FString()) + GuidString(Right.NodeGuid);
    });
    const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
    Summary->SetBoolField(TEXT("referenced"), bReferenced);
    Summary->SetNumberField(TEXT("reference_count"), Nodes.Num());
    Summary->SetBoolField(TEXT("unresolved_references"), bReferenced && Nodes.IsEmpty());
    Summary->SetBoolField(TEXT("references_truncated"), Nodes.Num() > UnrealMCP::MaxVariableReferences);
    TArray<TSharedPtr<FJsonValue>> References;
    const int32 Count = FMath::Min(Nodes.Num(), UnrealMCP::MaxVariableReferences);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        UK2Node* Node = Nodes[Index];
        if (Node == nullptr) continue;
        const TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
        const UEdGraph* Graph = Node->GetGraph();
        Reference->SetStringField(TEXT("graph_id"), Graph != nullptr ? GuidString(Graph->GraphGuid) : FString());
        Reference->SetStringField(TEXT("node_id"), GuidString(Node->NodeGuid));
        Reference->SetStringField(TEXT("node_class"), Node->GetClass()->GetPathName());
        Reference->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Left(256));
        References.Add(MakeShared<FJsonValueObject>(Reference));
    }
    Summary->SetArrayField(TEXT("references"), References);
    return Summary;
}

TSharedRef<FJsonObject> NodeReferenceSummary(TArray<UK2Node*> Nodes, bool bReferenced)
{
    Nodes.Sort([](const UK2Node& Left, const UK2Node& Right)
    {
        const UEdGraph* LeftGraph = Left.GetGraph();
        const UEdGraph* RightGraph = Right.GetGraph();
        return (LeftGraph != nullptr ? GuidString(LeftGraph->GraphGuid) : FString()) + GuidString(Left.NodeGuid)
            < (RightGraph != nullptr ? GuidString(RightGraph->GraphGuid) : FString()) + GuidString(Right.NodeGuid);
    });
    const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
    Summary->SetBoolField(TEXT("referenced"), bReferenced);
    Summary->SetNumberField(TEXT("reference_count"), Nodes.Num());
    Summary->SetBoolField(TEXT("unresolved_references"), bReferenced && Nodes.IsEmpty());
    Summary->SetBoolField(TEXT("references_truncated"), Nodes.Num() > UnrealMCP::MaxVariableReferences);
    TArray<TSharedPtr<FJsonValue>> References;
    for (int32 Index = 0; Index < FMath::Min(Nodes.Num(), UnrealMCP::MaxVariableReferences); ++Index)
    {
        UK2Node* Node = Nodes[Index];
        if (Node == nullptr) continue;
        const TSharedRef<FJsonObject> Reference = MakeShared<FJsonObject>();
        const UEdGraph* Graph = Node->GetGraph();
        Reference->SetStringField(TEXT("graph_id"), Graph != nullptr ? GuidString(Graph->GraphGuid) : FString());
        Reference->SetStringField(TEXT("node_id"), GuidString(Node->NodeGuid));
        Reference->SetStringField(TEXT("node_class"), Node->GetClass()->GetPathName());
        Reference->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Left(256));
        References.Add(MakeShared<FJsonValueObject>(Reference));
    }
    Summary->SetArrayField(TEXT("references"), References);
    return Summary;
}

TSharedRef<FJsonObject> FunctionReferences(UBlueprint* Blueprint, UEdGraph* FunctionGraph)
{
    TArray<UK2Node*> Nodes;
    TArray<UEdGraph*> AllGraphs;
    Blueprint->GetAllGraphs(AllGraphs);
    const FName FunctionName = FunctionGraph->GetFName();
    for (UEdGraph* Graph : AllGraphs)
    {
        if (Graph == nullptr) continue;
        for (UEdGraphNode* GraphNode : Graph->Nodes)
        {
            UK2Node* Node = Cast<UK2Node>(GraphNode);
            if (Node != nullptr && !Node->IsA<UK2Node_FunctionEntry>() && !Node->IsA<UK2Node_FunctionResult>()
                && Node->ReferencesFunction(FunctionName, Blueprint->SkeletonGeneratedClass))
            {
                Nodes.Add(Node);
            }
        }
    }
    const bool bReferenced = !Nodes.IsEmpty() || FBlueprintEditorUtils::IsFunctionUsed(Blueprint, FunctionName);
    return NodeReferenceSummary(MoveTemp(Nodes), bReferenced);
}

TSharedRef<FJsonObject> LocalReferences(UBlueprint* Blueprint, UEdGraph* FunctionGraph, const FName VariableName)
{
    TArray<UK2Node*> Nodes;
    const UStruct* Scope = Blueprint->SkeletonGeneratedClass != nullptr
        ? Blueprint->SkeletonGeneratedClass->FindFunctionByName(FunctionGraph->GetFName()) : nullptr;
    for (UEdGraphNode* GraphNode : FunctionGraph->Nodes)
    {
        UK2Node* Node = Cast<UK2Node>(GraphNode);
        if (Node != nullptr && Node->ReferencesVariable(VariableName, Scope)) Nodes.Add(Node);
    }
    const bool bReferenced = !Nodes.IsEmpty() || FBlueprintEditorUtils::IsVariableUsed(Blueprint, VariableName, FunctionGraph);
    return NodeReferenceSummary(MoveTemp(Nodes), bReferenced);
}

UK2Node_FunctionEntry* FunctionEntry(UEdGraph* Graph)
{
    return Graph != nullptr ? Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph)) : nullptr;
}

FString FunctionOwnership(const UBlueprint* Blueprint, const UEdGraph* Graph)
{
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
    {
        if (Interface.Graphs.Contains(Graph)) return TEXT("interface");
    }
    UK2Node_FunctionEntry* Entry = FunctionEntry(const_cast<UEdGraph*>(Graph));
    return Entry != nullptr && Entry->IsEditable() ? TEXT("user_owned") : TEXT("override");
}

FString FunctionAccess(int32 Flags)
{
    if ((Flags & FUNC_Private) != 0) return TEXT("private");
    if ((Flags & FUNC_Protected) != 0) return TEXT("protected");
    return TEXT("public");
}

TSharedRef<FJsonObject> FunctionMetadata(const UK2Node_FunctionEntry* Entry)
{
    const TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
    Metadata->SetStringField(TEXT("category"), Entry->MetaData.Category.ToString().Left(128));
    Metadata->SetStringField(TEXT("tooltip"), Entry->MetaData.ToolTip.ToString().Left(512));
    Metadata->SetStringField(TEXT("keywords"), Entry->MetaData.Keywords.ToString().Left(256));
    Metadata->SetBoolField(TEXT("call_in_editor"), Entry->MetaData.bCallInEditor);
    return Metadata;
}

TSharedRef<FJsonObject> FunctionSignature(const UK2Node_FunctionEntry* Entry, const TArray<UK2Node_FunctionResult*>& Results)
{
    const int32 Flags = Entry->GetFunctionFlags();
    const TSharedRef<FJsonObject> Signature = MakeShared<FJsonObject>();
    Signature->SetStringField(TEXT("access"), FunctionAccess(Flags));
    Signature->SetBoolField(TEXT("pure"), (Flags & FUNC_BlueprintPure) != 0);
    Signature->SetBoolField(TEXT("const"), (Flags & FUNC_Const) != 0);
    TArray<TSharedPtr<FJsonValue>> Parameters;
    auto Append = [&Parameters](const UK2Node_EditablePinBase* Node, const TCHAR* Direction)
    {
        if (Node == nullptr) return;
        for (const TSharedPtr<FUserPinInfo>& Pin : Node->UserDefinedPins)
        {
            if (!Pin.IsValid()) continue;
            const TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
            Parameter->SetStringField(TEXT("name"), Pin->PinName.ToString());
            Parameter->SetStringField(TEXT("direction"), Direction);
            Parameter->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(Pin->PinType));
            if (FCString::Strcmp(Direction, TEXT("input")) == 0 && !Pin->PinType.bIsReference)
            {
                Parameter->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(Pin->PinType, Pin->PinDefaultValue));
            }
            Parameters.Add(MakeShared<FJsonValueObject>(Parameter));
        }
    };
    Append(Entry, TEXT("input"));
    if (!Results.IsEmpty()) Append(Results[0], TEXT("output"));
    Signature->SetArrayField(TEXT("parameters"), Parameters);
    return Signature;
}

TSharedRef<FJsonObject> VariableMetadata(const FBPVariableDescription& Variable)
{
    const TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
    Metadata->SetStringField(TEXT("category"), Variable.Category.ToString().Left(128));
    Metadata->SetStringField(TEXT("tooltip"), Variable.HasMetaData(TEXT("tooltip")) ? Variable.GetMetaData(TEXT("tooltip")).Left(512) : FString());
    Metadata->SetBoolField(TEXT("instance_editable"), (Variable.PropertyFlags & CPF_Edit) != 0
        && (Variable.PropertyFlags & CPF_DisableEditOnInstance) == 0);
    Metadata->SetBoolField(TEXT("blueprint_visible"), (Variable.PropertyFlags & CPF_BlueprintVisible) != 0);
    Metadata->SetBoolField(TEXT("blueprint_read_only"), (Variable.PropertyFlags & CPF_BlueprintReadOnly) != 0);
    Metadata->SetBoolField(TEXT("expose_on_spawn"), Variable.HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn));
    Metadata->SetBoolField(TEXT("private"), Variable.HasMetaData(FBlueprintMetadata::MD_Private));
    Metadata->SetBoolField(TEXT("save_game"), (Variable.PropertyFlags & CPF_SaveGame) != 0);
    Metadata->SetBoolField(TEXT("advanced_display"), (Variable.PropertyFlags & CPF_AdvancedDisplay) != 0);
    return Metadata;
}

TSharedRef<FJsonObject> VariableReplication(UBlueprint* Blueprint, const FBPVariableDescription& Variable, const bool bMutable)
{
    const TSharedRef<FJsonObject> Replication = MakeShared<FJsonObject>();
    const bool bRepNotify = !Variable.RepNotifyFunc.IsNone() || (Variable.PropertyFlags & CPF_RepNotify) != 0;
    Replication->SetStringField(TEXT("mode"), bRepNotify ? TEXT("rep_notify")
        : (Variable.PropertyFlags & CPF_Net) != 0 ? TEXT("replicated") : TEXT("none"));
    Replication->SetStringField(TEXT("condition"), StaticEnum<ELifetimeCondition>()->GetNameStringByValue(Variable.ReplicationCondition));
    Replication->SetStringField(TEXT("rep_notify_function"), Variable.RepNotifyFunc.ToString());
    UEdGraph* NotifyGraph = nullptr;
    if (Blueprint != nullptr && !Variable.RepNotifyFunc.IsNone())
    {
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph != nullptr && Graph->GetFName() == Variable.RepNotifyFunc) { NotifyGraph = Graph; break; }
        }
    }
    UK2Node_FunctionEntry* NotifyEntry = FunctionEntry(NotifyGraph);
    TArray<UK2Node_FunctionResult*> NotifyResults;
    if (NotifyGraph != nullptr) NotifyGraph->GetNodesOfClass(NotifyResults);
    bool bHasOutputs = false;
    for (UK2Node_FunctionResult* Result : NotifyResults) bHasOutputs |= Result != nullptr && !Result->UserDefinedPins.IsEmpty();
    const bool bRelationshipValid = !bRepNotify || (NotifyGraph != nullptr && NotifyEntry != nullptr
        && FunctionOwnership(Blueprint, NotifyGraph) == TEXT("user_owned")
        && NotifyEntry->UserDefinedPins.IsEmpty() && !bHasOutputs && (NotifyEntry->GetFunctionFlags() & FUNC_BlueprintPure) == 0);
    Replication->SetStringField(TEXT("rep_notify_function_id"), NotifyGraph != nullptr ? GuidString(NotifyGraph->GraphGuid) : FString());
    Replication->SetBoolField(TEXT("relationship_valid"), bRelationshipValid);
    Replication->SetBoolField(TEXT("rep_notify_mutable"), bMutable);
    return Replication;
}

FString VariableDefaultText(UBlueprint* Blueprint, const FBPVariableDescription& Variable)
{
    if (Blueprint != nullptr && Blueprint->GeneratedClass != nullptr)
    {
        UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject(false);
        FProperty* Property = Blueprint->GeneratedClass->FindPropertyByName(Variable.VarName);
        if (Defaults != nullptr && Property != nullptr)
        {
            FString Text;
            Property->ExportText_InContainer(0, Text, Defaults, Defaults->GetArchetype(), Defaults, PPF_None);
            return Text;
        }
    }
    return Variable.DefaultValue;
}

FString GraphKind(const UBlueprint* Blueprint, const UEdGraph* Graph)
{
    if (Blueprint->UbergraphPages.Contains(Graph)) return TEXT("event");
    if (Blueprint->FunctionGraphs.Contains(Graph)) return TEXT("function");
    if (Blueprint->MacroGraphs.Contains(Graph)) return TEXT("macro");
    if (Blueprint->DelegateSignatureGraphs.Contains(Graph)) return TEXT("delegate");
    return TEXT("other");
}

void AddBlueprintGraphs(UBlueprint* Blueprint, const FString& OwnerPath, TArray<TPair<UEdGraph*, FString>>& OutGraphs)
{
    auto Append = [&OutGraphs, &OwnerPath](const TArray<TObjectPtr<UEdGraph>>& Source)
    {
        for (UEdGraph* Graph : Source)
        {
            if (Graph != nullptr)
            {
                OutGraphs.Emplace(Graph, OwnerPath);
            }
        }
    };
    Append(Blueprint->UbergraphPages);
    Append(Blueprint->FunctionGraphs);
    Append(Blueprint->MacroGraphs);
    Append(Blueprint->DelegateSignatureGraphs);
}

bool ReadPropertyNames(const FJsonObject& Arguments, TSet<FString>& OutNames, FUnrealMCPError& OutError)
{
    if (!Arguments.HasField(TEXT("property_names"))) return true;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Arguments.TryGetArrayField(TEXT("property_names"), Values) || Values == nullptr || Values->IsEmpty()
        || Values->Num() > UnrealMCP::MaxPropertyNames)
    {
        OutError = {TEXT("invalid_argument"), TEXT("property_names must be a non-empty bounded array")};
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Item : *Values)
    {
        FString Name;
        if (!Item.IsValid() || !Item->TryGetString(Name) || Name.IsEmpty() || Name.Len() > 128 || Name.Contains(TEXT(".")) || OutNames.Contains(Name))
        {
            OutError = {TEXT("invalid_argument"), TEXT("property_names contains an invalid or duplicate exact property name")};
            return false;
        }
        OutNames.Add(Name);
    }
    return true;
}

FString AddComponentDefaults(UActorComponent* Template, const TSet<FString>& RequestedProperties, const TSharedRef<FJsonObject>& Component)
{
    TArray<FProperty*> Changed;
    UObject* Archetype = Template->GetArchetype();
    for (TFieldIterator<FProperty> It(Template->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
    {
        FProperty* Property = *It;
        if (Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_Transient)
            && (Archetype == nullptr || !Property->Identical_InContainer(Template, Archetype)))
        {
            Changed.Add(Property);
        }
    }
    Changed.Sort([](const FProperty& Left, const FProperty& Right) { return Left.GetName() < Right.GetName(); });
    TArray<TSharedPtr<FJsonValue>> Defaults;
    TArray<FString> Fingerprint;
    const int32 Count = FMath::Min(Changed.Num(), UnrealMCP::MaxComponentDefaults);
    for (int32 Index = 0; Index < Changed.Num(); ++Index)
    {
        FProperty* Property = Changed[Index];
        FString Kind;
        const bool bSupported = UnrealMCP::PropertyCodec::IsSupportedEditable(Property, Kind);
        FString Encoded;
        Property->ExportText_InContainer(0, Encoded, Template, Archetype, Template, PPF_None);
        Fingerprint.Add(Property->GetName() + TEXT("|") + (bSupported ? Kind : TEXT("unsupported")) + TEXT("|") + Encoded);
        if (Index < Count) Defaults.Add(MakeShared<FJsonValueObject>(UnrealMCP::PropertyCodec::Encode(Template, Property)));
    }
    Component->SetArrayField(TEXT("changed_defaults"), Defaults);
    Component->SetNumberField(TEXT("changed_default_count"), Changed.Num());
    Component->SetBoolField(TEXT("defaults_truncated"), Changed.Num() > Count);
    if (!RequestedProperties.IsEmpty())
    {
        TArray<FString> Sorted = RequestedProperties.Array();
        Sorted.Sort();
        TArray<TSharedPtr<FJsonValue>> Editable;
        for (const FString& Name : Sorted)
        {
            Editable.Add(MakeShared<FJsonValueObject>(UnrealMCP::PropertyCodec::Encode(
                Template, Template->GetClass()->FindPropertyByName(FName(*Name)))));
        }
        Component->SetArrayField(TEXT("editable_properties"), Editable);
    }
    return FString::Join(Fingerprint, TEXT(";"));
}

void AddClassDefaultFingerprint(UBlueprint* Blueprint, TArray<FString>& Fingerprint)
{
    UObject* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
    if (Defaults == nullptr) return;
    UObject* Archetype = Defaults->GetArchetype();
    for (TFieldIterator<FProperty> It(Defaults->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
    {
        FProperty* Property = *It;
        FString Kind;
        if (UnrealMCP::PropertyCodec::IsSupportedEditable(Property, Kind)
            && (Archetype == nullptr || !Property->Identical_InContainer(Defaults, Archetype)))
        {
            FString Encoded;
            Property->ExportText_InContainer(0, Encoded, Defaults, Archetype, Defaults, PPF_None);
            Fingerprint.Add(TEXT("class_default|") + Property->GetName() + TEXT("|") + Kind + TEXT("|") + Encoded);
        }
    }
}

bool AssetIsActorBlueprint(const FAssetData& Asset)
{
    FString NativeParent;
    if (!Asset.GetTagValue(FBlueprintTags::NativeParentClassPath, NativeParent))
    {
        return false;
    }
    const FString ObjectPath = FPackageName::ExportTextPathToObjectPath(NativeParent);
    UClass* NativeClass = FindObject<UClass>(nullptr, *ObjectPath);
    return NativeClass != nullptr && NativeClass->IsChildOf(AActor::StaticClass());
}

bool BuildDiscovery(
    const FJsonObject& Arguments,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    FString& OutSnapshot,
    bool& OutScanTruncated,
    FUnrealMCPError& OutError)
{
    if (!HasOnlyFields(Arguments, {TEXT("mode"), TEXT("package_path"), TEXT("asset_name"), TEXT("page_size")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Discovery arguments contain an unknown field")};
        return false;
    }
    FString PackagePath;
    FString RawPackagePath;
    if (Arguments.HasField(TEXT("package_path")) && !Arguments.TryGetStringField(TEXT("package_path"), RawPackagePath))
    {
        OutError = {TEXT("invalid_argument"), TEXT("package_path must be a string")};
        return false;
    }
    if (!NormalizePackagePath(RawPackagePath, PackagePath))
    {
        OutError = {TEXT("invalid_argument"), TEXT("package_path must be a valid mounted-content package path")};
        return false;
    }
    FString AssetName;
    if (Arguments.HasField(TEXT("asset_name"))
        && (!Arguments.TryGetStringField(TEXT("asset_name"), AssetName) || AssetName.IsEmpty() || AssetName.Len() > 128))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_name must be a non-empty bounded string")};
        return false;
    }

    FARFilter Filter;
    if (!PackagePath.IsEmpty())
    {
        Filter.PackagePaths.Add(FName(*PackagePath));
    }
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;
    Filter.bRecursiveClasses = true;
    TArray<FAssetData> Assets;
    int32 Scanned = 0;
    OutScanTruncated = false;
    FAssetRegistryModule::GetRegistry().EnumerateAssets(Filter, [&Assets, &Scanned, &OutScanTruncated](const FAssetData& Asset)
    {
        if (Scanned++ >= UnrealMCP::MaxDiscoveryScan)
        {
            OutScanTruncated = true;
            return false;
        }
        Assets.Add(Asset);
        return true;
    });
    Assets.Sort([](const FAssetData& Left, const FAssetData& Right)
    {
        return Left.GetObjectPathString() < Right.GetObjectPathString();
    });
    TArray<FString> Fingerprint;
    for (int32 Index = 0; Index < Assets.Num(); ++Index)
    {
        const FAssetData& Asset = Assets[Index];
        if ((!AssetName.IsEmpty() && Asset.AssetName.ToString() != AssetName) || !AssetIsActorBlueprint(Asset))
        {
            continue;
        }
        const TSharedRef<FJsonObject> Value = Record(TEXT("asset"));
        Value->SetStringField(TEXT("asset_path"), Asset.GetObjectPathString());
        Value->SetStringField(TEXT("package_path"), Asset.PackagePath.ToString());
        Value->SetStringField(TEXT("asset_name"), Asset.AssetName.ToString());
        FString Parent;
        if (Asset.GetTagValue(FBlueprintTags::ParentClassPath, Parent))
        {
            Parent = FPackageName::ExportTextPathToObjectPath(Parent);
        }
        Value->SetStringField(TEXT("parent_class"), Parent);
        AddRecord(OutRecords, Value);
        Fingerprint.Add(Asset.GetObjectPathString() + TEXT("|") + Parent);
    }
    OutSnapshot = HashLines(MoveTemp(Fingerprint));
    return true;
}

bool BuildInspection(
    const FJsonObject& Arguments,
    TArray<TSharedPtr<FJsonValue>>& OutRecords,
    FString& OutSnapshot,
    bool& OutScanTruncated,
    FUnrealMCPError& OutError)
{
    OutScanTruncated = false;
    if (!HasOnlyFields(Arguments, {TEXT("mode"), TEXT("asset_path"), TEXT("sections"), TEXT("graph_id"), TEXT("component_id"), TEXT("member_id"),
        TEXT("function_id"), TEXT("local_id"),
        TEXT("property_names"), TEXT("include_inherited"), TEXT("page_size")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Inspection arguments contain an unknown field")};
        return false;
    }
    FString RawAssetPath;
    FString AssetPath;
    if (!Arguments.TryGetStringField(TEXT("asset_path"), RawAssetPath) || !NormalizeAssetPath(RawAssetPath, AssetPath))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_path must identify one valid mounted-content asset")};
        return false;
    }
    bool bIncludeInherited = false;
    if (!ReadOptionalBool(Arguments, TEXT("include_inherited"), false, bIncludeInherited, OutError))
    {
        return false;
    }
    TSet<FString> Sections = {TEXT("summary"), TEXT("parent_class"), TEXT("compile_state"), TEXT("components"),
        TEXT("variables"), TEXT("functions"), TEXT("local_variables"), TEXT("graphs")};
    if (Arguments.HasField(TEXT("sections")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Arguments.TryGetArrayField(TEXT("sections"), Values) || Values == nullptr || Values->IsEmpty() || Values->Num() > 13)
        {
            OutError = {TEXT("invalid_argument"), TEXT("sections must be a non-empty bounded array")};
            return false;
        }
        Sections.Reset();
        for (const TSharedPtr<FJsonValue>& Item : *Values)
        {
            FString Section;
            if (!Item.IsValid() || !Item->TryGetString(Section) || !InspectSections.Contains(Section) || Sections.Contains(Section))
            {
                OutError = {TEXT("invalid_argument"), TEXT("sections contains an invalid or duplicate value")};
                return false;
            }
            Sections.Add(Section);
        }
    }
    FString GraphFilter;
    if (Arguments.HasField(TEXT("graph_id"))
        && (!Arguments.TryGetStringField(TEXT("graph_id"), GraphFilter) || GraphFilter.Len() != 32))
    {
        OutError = {TEXT("invalid_argument"), TEXT("graph_id must be a 32-character graph identity")};
        return false;
    }
    FString ComponentFilter;
    if (Arguments.HasField(TEXT("component_id"))
        && (!Arguments.TryGetStringField(TEXT("component_id"), ComponentFilter) || ComponentFilter.Len() != 32))
    {
        OutError = {TEXT("invalid_argument"), TEXT("component_id must be a 32-character stable component identity")};
        return false;
    }
    FString MemberFilter;
    if (Arguments.HasField(TEXT("member_id"))
        && (!Arguments.TryGetStringField(TEXT("member_id"), MemberFilter) || MemberFilter.Len() != 32))
    {
        OutError = {TEXT("invalid_argument"), TEXT("member_id must be a 32-character stable member identity")};
        return false;
    }
    FString FunctionFilter;
    if (Arguments.HasField(TEXT("function_id"))
        && (!Arguments.TryGetStringField(TEXT("function_id"), FunctionFilter) || FunctionFilter.Len() != 32))
    {
        OutError = {TEXT("invalid_argument"), TEXT("function_id must be a 32-character stable function identity")};
        return false;
    }
    FString LocalFilter;
    if (Arguments.HasField(TEXT("local_id"))
        && (!Arguments.TryGetStringField(TEXT("local_id"), LocalFilter) || LocalFilter.Len() != 32))
    {
        OutError = {TEXT("invalid_argument"), TEXT("local_id must be a 32-character stable local-variable identity")};
        return false;
    }
    TSet<FString> PropertyNames;
    if (!ReadPropertyNames(Arguments, PropertyNames, OutError)) return false;
    if (Sections.Contains(TEXT("class_defaults")) && PropertyNames.IsEmpty())
    {
        OutError = {TEXT("invalid_argument"), TEXT("class_defaults inspection requires one or more targeted property_names")};
        return false;
    }

    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    const FAssetData Asset = Registry.GetAssetByObjectPath(FSoftObjectPath(AssetPath));
    if (!Asset.IsValid())
    {
        OutError = {TEXT("not_found"), TEXT("The requested asset was not found")};
        return false;
    }
    const bool bWasLoaded = Asset.IsAssetLoaded();
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
    if (Blueprint == nullptr)
    {
        OutError = {TEXT("wrong_type"), TEXT("The requested asset is not a Blueprint")};
        return false;
    }
    if (Blueprint->ParentClass == nullptr || !Blueprint->ParentClass->IsChildOf(AActor::StaticClass()))
    {
        OutError = {TEXT("wrong_type"), TEXT("The requested Blueprint is not Actor-derived")};
        return false;
    }
    UPackage* Package = Blueprint->GetOutermost();
    const bool bDirtyBefore = Package->IsDirty();
    const EBlueprintStatus StatusBefore = Blueprint->Status;
    TArray<FString> Fingerprint;
    AddClassDefaultFingerprint(Blueprint, Fingerprint);

    if (Sections.Contains(TEXT("summary")))
    {
        const TSharedRef<FJsonObject> Value = Record(TEXT("summary"));
        Value->SetStringField(TEXT("asset_path"), AssetPath);
        Value->SetStringField(TEXT("asset_name"), Blueprint->GetName());
        Value->SetBoolField(TEXT("was_loaded"), bWasLoaded);
        Value->SetBoolField(TEXT("package_dirty"), bDirtyBefore);
        Value->SetBoolField(TEXT("actor_blueprint"), true);
        AddRecord(OutRecords, Value);
    }
    if (Sections.Contains(TEXT("parent_class")))
    {
        const TSharedRef<FJsonObject> Value = Record(TEXT("parent_class"));
        Value->SetStringField(TEXT("class_path"), Blueprint->ParentClass->GetPathName());
        Value->SetBoolField(TEXT("blueprint_generated"), UBlueprint::GetBlueprintFromClass(Blueprint->ParentClass) != nullptr);
        AddRecord(OutRecords, Value);
    }
    if (Sections.Contains(TEXT("compile_state")))
    {
        const TSharedRef<FJsonObject> Value = Record(TEXT("compile_state"));
        Value->SetStringField(TEXT("state"), CompileState(Blueprint->Status));
        Value->SetBoolField(TEXT("being_compiled"), Blueprint->bBeingCompiled != 0);
        AddRecord(OutRecords, Value);
    }

    TArray<TPair<UBlueprint*, FString>> Owners;
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
                AddRecord(OutRecords, Value);
            }
            Fingerprint.Add(TEXT("component|") + Owner.Value + TEXT("|") + Id + TEXT("|") + Node->GetVariableName().ToString()
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
                    AddRecord(OutRecords, Value);
                }
                Fingerprint.Add(TEXT("native_component|") + Component->GetName() + TEXT("|")
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
            AddRecord(OutRecords, Value);
        }
    }

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
                AddRecord(OutRecords, Value);
            }
            TArray<FString> MetadataFingerprint;
            for (const FBPVariableMetaDataEntry& Entry : Variable.MetaDataArray)
            {
                MetadataFingerprint.Add(Entry.DataKey.ToString() + TEXT("=") + Entry.DataValue);
            }
            MetadataFingerprint.Sort();
            Fingerprint.Add(TEXT("variable|") + Owner.Value + TEXT("|") + GuidString(Variable.VarGuid) + TEXT("|") + Variable.VarName.ToString()
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
                Fingerprint.Add(TEXT("function|missing_entry|") + Owner.Value + TEXT("|") + FunctionId + TEXT("|") + FunctionGraph->GetName());
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
                AddRecord(OutRecords, Value);
            }

            int32 ParameterIndex = 0;
            auto AppendParameters = [&](UK2Node_EditablePinBase* Node, const TCHAR* Direction)
            {
                if (Node == nullptr) return;
                for (const TSharedPtr<FUserPinInfo>& Pin : Node->UserDefinedPins)
                {
                    if (!Pin.IsValid()) continue;
                    UEdGraphPin* LivePin = Node->FindPin(Pin->PinName);
                    if (Sections.Contains(TEXT("parameters")))
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
                        AddRecord(OutRecords, Value);
                    }
                    Fingerprint.Add(TEXT("parameter|") + FunctionId + TEXT("|") + LexToString(ParameterIndex) + TEXT("|")
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
                    AddRecord(OutRecords, Value);
                }
                Fingerprint.Add(TEXT("local|") + FunctionId + TEXT("|") + LocalId + TEXT("|") + Local.VarName.ToString()
                    + TEXT("|") + VariableTypeFingerprint(Local.VarType) + TEXT("|") + Local.DefaultValue);
            }
            Fingerprint.Add(TEXT("function|") + Owner.Value + TEXT("|") + FunctionId + TEXT("|") + FunctionGraph->GetName()
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

    TArray<TPair<UEdGraph*, FString>> Graphs;
    for (const TPair<UBlueprint*, FString>& Owner : Owners) AddBlueprintGraphs(Owner.Key, Owner.Value, Graphs);
    Graphs.Sort([](const TPair<UEdGraph*, FString>& Left, const TPair<UEdGraph*, FString>& Right)
    {
        const FString A = Left.Value + TEXT("|") + GuidString(Left.Key->GraphGuid) + TEXT("|") + Left.Key->GetName();
        const FString B = Right.Value + TEXT("|") + GuidString(Right.Key->GraphGuid) + TEXT("|") + Right.Key->GetName();
        return A < B;
    });
    bool bGraphFound = GraphFilter.IsEmpty();
    for (const TPair<UEdGraph*, FString>& Entry : Graphs)
    {
        UEdGraph* Graph = Entry.Key;
        const FString GraphId = GuidString(Graph->GraphGuid);
        if (!GraphFilter.IsEmpty() && GraphId != GraphFilter) continue;
        bGraphFound = true;
        UBlueprint* OwnerBlueprint = Graph->GetTypedOuter<UBlueprint>();
        const FString Kind = OwnerBlueprint != nullptr ? GraphKind(OwnerBlueprint, Graph) : TEXT("other");
        if (Sections.Contains(TEXT("graphs")))
        {
            const TSharedRef<FJsonObject> Value = Record(TEXT("graph"));
            Value->SetStringField(TEXT("id"), GraphId);
            Value->SetBoolField(TEXT("identity_stable"), !GraphId.IsEmpty());
            Value->SetStringField(TEXT("name"), Graph->GetName());
            Value->SetStringField(TEXT("kind"), Kind);
            Value->SetStringField(TEXT("owner_blueprint"), Entry.Value);
            Value->SetBoolField(TEXT("inherited"), OwnerBlueprint != Blueprint);
            Value->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
            AddRecord(OutRecords, Value);
        }
        Fingerprint.Add(TEXT("graph|") + Entry.Value + TEXT("|") + GraphId + TEXT("|") + Graph->GetName() + TEXT("|") + Kind);
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr) continue;
            const FString NodeId = GuidString(Node->NodeGuid);
            if (Sections.Contains(TEXT("nodes")))
            {
                const TSharedRef<FJsonObject> Value = Record(TEXT("node"));
                Value->SetStringField(TEXT("graph_id"), GraphId);
                Value->SetStringField(TEXT("id"), NodeId);
                Value->SetBoolField(TEXT("identity_stable"), !NodeId.IsEmpty());
                Value->SetStringField(TEXT("class_path"), Node->GetClass()->GetPathName());
                Value->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Left(256));
                Value->SetNumberField(TEXT("x"), Node->NodePosX);
                Value->SetNumberField(TEXT("y"), Node->NodePosY);
                AddRecord(OutRecords, Value);
            }
            Fingerprint.Add(TEXT("node|") + GraphId + TEXT("|") + NodeId + TEXT("|") + Node->GetClass()->GetPathName()
                + FString::Printf(TEXT("|%d|%d"), Node->NodePosX, Node->NodePosY));
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin == nullptr) continue;
                const FString PinId = GuidString(Pin->PinId);
                if (Sections.Contains(TEXT("pins")))
                {
                    const TSharedRef<FJsonObject> Value = Record(TEXT("pin"));
                    Value->SetStringField(TEXT("graph_id"), GraphId);
                    Value->SetStringField(TEXT("node_id"), NodeId);
                    Value->SetStringField(TEXT("id"), PinId);
                    Value->SetBoolField(TEXT("identity_stable"), !PinId.IsEmpty());
                    Value->SetStringField(TEXT("name"), Pin->PinName.ToString());
                    Value->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
                    Value->SetObjectField(TEXT("type"), PinType(Pin->PinType));
                    Value->SetStringField(TEXT("default_value"), Pin->DefaultValue.Left(512));
                    if (Pin->DefaultObject != nullptr) Value->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
                    AddRecord(OutRecords, Value);
                }
                Fingerprint.Add(TEXT("pin|") + GraphId + TEXT("|") + NodeId + TEXT("|") + PinId + TEXT("|")
                    + Pin->PinName.ToString() + TEXT("|") + Pin->PinType.PinCategory.ToString() + TEXT("|") + Pin->DefaultValue);
                if (Pin->Direction == EGPD_Output)
                {
                    for (UEdGraphPin* Linked : Pin->LinkedTo)
                    {
                        if (Linked == nullptr || Linked->GetOwningNodeUnchecked() == nullptr) continue;
                        if (Sections.Contains(TEXT("connections")))
                        {
                            const TSharedRef<FJsonObject> Value = Record(TEXT("connection"));
                            Value->SetStringField(TEXT("graph_id"), GraphId);
                            Value->SetStringField(TEXT("from_node_id"), NodeId);
                            Value->SetStringField(TEXT("from_pin_id"), PinId);
                            Value->SetStringField(TEXT("to_node_id"), GuidString(Linked->GetOwningNodeUnchecked()->NodeGuid));
                            Value->SetStringField(TEXT("to_pin_id"), GuidString(Linked->PinId));
                            AddRecord(OutRecords, Value);
                        }
                        Fingerprint.Add(TEXT("link|") + PinId + TEXT("|") + GuidString(Linked->PinId));
                    }
                }
            }
        }
    }
    if (!bGraphFound)
    {
        OutError = {TEXT("not_found"), TEXT("The requested graph identity was not found")};
        return false;
    }
    if (OutRecords.Num() > UnrealMCP::MaxInspectRecords || Fingerprint.Num() > UnrealMCP::MaxInspectRecords)
    {
        OutError = {TEXT("response_too_large"), TEXT("Inspection exceeds the configured structural record limit")};
        return false;
    }
    if (Package->IsDirty() != bDirtyBefore || Blueprint->Status != StatusBefore)
    {
        OutError = {TEXT("internal_error"), TEXT("Inspection unexpectedly changed Blueprint state")};
        return false;
    }
    OutSnapshot = HashLines(MoveTemp(Fingerprint));
    return true;
}
}

FUnrealMCPBlueprintInspector::FUnrealMCPBlueprintInspector(TFunction<double()> InNow)
    : Now(MoveTemp(InNow))
{
}

void FUnrealMCPBlueprintInspector::RemoveExpiredCursors(double CurrentTime)
{
    for (auto It = Cursors.CreateIterator(); It; ++It)
    {
        if (It.Value().ExpiresAt <= CurrentTime)
        {
            It.RemoveCurrent();
        }
    }
}

bool FUnrealMCPBlueprintInspector::Execute(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    check(IsInGameThread());
    if (!Arguments.IsValid())
    {
        OutError = {TEXT("invalid_argument"), TEXT("arguments must be an object")};
        return false;
    }
    const double CurrentTime = Now();
    RemoveExpiredCursors(CurrentTime);
    if (!Arguments->HasField(TEXT("cursor")))
    {
        return ExecuteInitial(Arguments, 0, FString(), INDEX_NONE, OutResult, OutError);
    }
    if (!HasOnlyFields(*Arguments, {TEXT("cursor"), TEXT("page_size")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("Cursor continuation accepts only cursor and page_size")};
        return false;
    }
    FString Cursor;
    if (!Arguments->TryGetStringField(TEXT("cursor"), Cursor) || Cursor.Len() != 32)
    {
        OutError = {TEXT("invalid_argument"), TEXT("cursor must be a 32-character opaque value")};
        return false;
    }
    FCursorState* State = Cursors.Find(Cursor);
    if (State == nullptr)
    {
        OutError = {TEXT("cursor_expired"), TEXT("The inspection cursor is missing or expired"), MakeShared<FJsonObject>(), true};
        return false;
    }
    int32 PageSize = UnrealMCP::DefaultInspectPageSize;
    if (!ReadPageSize(*Arguments, PageSize, OutError))
    {
        return false;
    }
    const FCursorState Saved = *State;
    Cursors.Remove(Cursor);
    return ExecuteInitial(Saved.Arguments, Saved.Offset, Saved.SnapshotId, PageSize, OutResult, OutError);
}

bool FUnrealMCPBlueprintInspector::ExecuteInitial(
    const TSharedPtr<FJsonObject>& Arguments,
    int32 Offset,
    const FString& ExpectedSnapshot,
    int32 PageSizeOverride,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    FString Mode;
    if (!Arguments->TryGetStringField(TEXT("mode"), Mode) || (Mode != TEXT("discover") && Mode != TEXT("inspect")))
    {
        OutError = {TEXT("invalid_argument"), TEXT("mode must be discover or inspect")};
        return false;
    }
    int32 PageSize = UnrealMCP::DefaultInspectPageSize;
    if (!ReadPageSize(*Arguments, PageSize, OutError))
    {
        return false;
    }
    if (PageSizeOverride != INDEX_NONE)
    {
        PageSize = PageSizeOverride;
    }
    TArray<TSharedPtr<FJsonValue>> Records;
    FString Snapshot;
    bool bScanTruncated = false;
    const bool bBuilt = Mode == TEXT("discover")
        ? BuildDiscovery(*Arguments, Records, Snapshot, bScanTruncated, OutError)
        : BuildInspection(*Arguments, Records, Snapshot, bScanTruncated, OutError);
    if (!bBuilt)
    {
        return false;
    }
    if (!ExpectedSnapshot.IsEmpty() && Snapshot != ExpectedSnapshot)
    {
        OutError = {TEXT("stale_precondition"), TEXT("The structural snapshot changed before the cursor was continued")};
        return false;
    }
    if (Offset < 0 || Offset > Records.Num())
    {
        OutError = {TEXT("cursor_expired"), TEXT("The inspection cursor no longer identifies a valid page")};
        return false;
    }
    const int32 End = FMath::Min(Offset + PageSize, Records.Num());
    TArray<TSharedPtr<FJsonValue>> Page;
    for (int32 Index = Offset; Index < End; ++Index) Page.Add(Records[Index]);
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("mode"), Mode);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    Result->SetArrayField(TEXT("records"), Page);
    Result->SetNumberField(TEXT("record_count"), Records.Num());
    Result->SetNumberField(TEXT("page_offset"), Offset);
    Result->SetBoolField(TEXT("scan_truncated"), bScanTruncated);
    Result->SetBoolField(TEXT("has_more"), End < Records.Num());
    if (End < Records.Num())
    {
        RemoveExpiredCursors(Now());
        if (Cursors.Num() >= UnrealMCP::MaxRetainedCursors)
        {
            FString OldestKey;
            double Oldest = TNumericLimits<double>::Max();
            for (const TPair<FString, FCursorState>& Pair : Cursors)
            {
                if (Pair.Value.ExpiresAt < Oldest)
                {
                    Oldest = Pair.Value.ExpiresAt;
                    OldestKey = Pair.Key;
                }
            }
            Cursors.Remove(OldestKey);
        }
        const FString Cursor = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
        Cursors.Add(Cursor, FCursorState{Arguments, Snapshot, End, Now() + UnrealMCP::CursorLifetimeSeconds});
        Result->SetStringField(TEXT("next_cursor"), Cursor);
        Result->SetNumberField(TEXT("cursor_expires_in_ms"), static_cast<int32>(UnrealMCP::CursorLifetimeSeconds * 1000.0));
    }
    OutResult = Result;
    return true;
}
