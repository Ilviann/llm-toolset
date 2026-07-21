#pragma once

#include "UnrealMCPBlueprintInspector.h"

#include "UnrealMCPBlueprintReferenceScanner.h"

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
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
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

namespace UnrealMCP::BlueprintInspectionPrivate
{
struct FInspectionSink
{
    explicit FInspectionSink(TArray<TSharedPtr<FJsonValue>>& InRecords)
        : Records(InRecords)
    {
    }

    bool ExceedsStructuralLimit() const
    {
        return Records.Num() > UnrealMCP::MaxInspectRecords
            || Fingerprint.Num() > UnrealMCP::MaxInspectRecords;
    }

    TArray<TSharedPtr<FJsonValue>>& Records;
    TArray<FString> Fingerprint;
};

const TSet<FString> InspectSections = {
    TEXT("summary"), TEXT("parent_class"), TEXT("compile_state"), TEXT("components"),
    TEXT("class_defaults"), TEXT("variables"), TEXT("functions"), TEXT("macros"), TEXT("custom_events"),
    TEXT("parameters"), TEXT("local_variables"),
    TEXT("graphs"), TEXT("nodes"), TEXT("pins"), TEXT("connections")};

const TSet<FString> SupportedPinCategories = {
    TEXT("exec"), TEXT("boolean"), TEXT("byte"), TEXT("int"), TEXT("int64"), TEXT("real"),
    TEXT("float"), TEXT("double"), TEXT("name"), TEXT("string"), TEXT("text"), TEXT("enum"),
    TEXT("struct"), TEXT("object"), TEXT("class"), TEXT("softobject"), TEXT("softclass"),
    TEXT("interface"), TEXT("wildcard")};

static FString GuidString(const FGuid& Guid)
{
    return Guid.IsValid() ? Guid.ToString(EGuidFormats::Digits).ToLower() : FString();
}

static FString HashLines(TArray<FString> Lines)
{
    Lines.Sort();
    const FString Joined = FString::Join(Lines, TEXT("\n"));
    FTCHARToUTF8 Encoded(*Joined);
    uint8 Digest[FSHA1::DigestSize];
    FSHA1::HashBuffer(Encoded.Get(), Encoded.Length(), Digest);
    return BytesToHex(Digest, FSHA1::DigestSize).ToLower();
}

static TSharedRef<FJsonObject> Record(const TCHAR* Section)
{
    const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
    Value->SetStringField(TEXT("section"), Section);
    return Value;
}

static void AddRecord(TArray<TSharedPtr<FJsonValue>>& Records, const TSharedRef<FJsonObject>& Value)
{
    Records.Add(MakeShared<FJsonValueObject>(Value));
}

static bool HasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed)
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

static bool ReadPageSize(const FJsonObject& Object, int32& OutPageSize, FUnrealMCPError& OutError)
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

static bool ReadOptionalBool(const FJsonObject& Object, const TCHAR* Name, bool DefaultValue, bool& OutValue, FUnrealMCPError& OutError)
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

static bool NormalizePackagePath(const FString& Input, FString& OutPath)
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

static bool NormalizeAssetPath(const FString& Input, FString& OutObjectPath)
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

static FString CompileState(EBlueprintStatus Status)
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

static FString ContainerName(EPinContainerType Type)
{
    switch (Type)
    {
    case EPinContainerType::Array: return TEXT("array");
    case EPinContainerType::Set: return TEXT("set");
    case EPinContainerType::Map: return TEXT("map");
    default: return TEXT("none");
    }
}

static FString VariableTypeFingerprint(const FEdGraphPinType& Type)
{
    const UObject* KeyTypeObject = Type.PinSubCategoryObject.Get();
    const UObject* ValueTypeObject = Type.PinValueType.TerminalSubCategoryObject.Get();
    return Type.PinCategory.ToString() + TEXT("|") + Type.PinSubCategory.ToString() + TEXT("|")
        + (KeyTypeObject != nullptr ? KeyTypeObject->GetPathName() : FString()) + TEXT("|")
        + ContainerName(Type.ContainerType) + TEXT("|") + Type.PinValueType.TerminalCategory.ToString() + TEXT("|")
        + Type.PinValueType.TerminalSubCategory.ToString() + TEXT("|")
        + (ValueTypeObject != nullptr ? ValueTypeObject->GetPathName() : FString());
}

static TSharedRef<FJsonObject> PinType(const FEdGraphPinType& Type)
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

static TSharedRef<FJsonObject> VariableReferences(UBlueprint* Blueprint, const FName VariableName)
{
    return UnrealMCP::BlueprintReferences::Encode(
        UnrealMCP::BlueprintReferences::ScanMemberVariable(Blueprint, VariableName));
}



static TSharedRef<FJsonObject> FunctionReferences(UBlueprint* Blueprint, UEdGraph* FunctionGraph)
{
    return UnrealMCP::BlueprintReferences::Encode(
        UnrealMCP::BlueprintReferences::ScanFunction(Blueprint, FunctionGraph));
}

static TSharedRef<FJsonObject> MacroReferences(UBlueprint* Blueprint, UEdGraph* MacroGraph)
{
    return UnrealMCP::BlueprintReferences::Encode(
        UnrealMCP::BlueprintReferences::ScanMacro(Blueprint, MacroGraph));
}

static TSharedRef<FJsonObject> CustomEventReferences(UBlueprint* Blueprint, UK2Node_CustomEvent* Event)
{
    return UnrealMCP::BlueprintReferences::Encode(
        UnrealMCP::BlueprintReferences::ScanCustomEvent(Blueprint, Event));
}

static TSharedRef<FJsonObject> LocalReferences(UBlueprint* Blueprint, UEdGraph* FunctionGraph, const FName VariableName)
{
    return UnrealMCP::BlueprintReferences::Encode(
        UnrealMCP::BlueprintReferences::ScanLocalVariable(Blueprint, FunctionGraph, VariableName));
}

UK2Node_FunctionEntry* FunctionEntry(UEdGraph* Graph)
{
    return Graph != nullptr ? Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph)) : nullptr;
}

static FString FunctionOwnership(const UBlueprint* Blueprint, const UEdGraph* Graph)
{
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
    {
        if (Interface.Graphs.Contains(Graph)) return TEXT("interface");
    }
    UK2Node_FunctionEntry* Entry = FunctionEntry(const_cast<UEdGraph*>(Graph));
    return Entry != nullptr && Entry->IsEditable() ? TEXT("user_owned") : TEXT("override");
}

static FString FunctionAccess(int32 Flags)
{
    if ((Flags & FUNC_Private) != 0) return TEXT("private");
    if ((Flags & FUNC_Protected) != 0) return TEXT("protected");
    return TEXT("public");
}

static TSharedRef<FJsonObject> FunctionMetadata(const UK2Node_FunctionEntry* Entry)
{
    const TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
    Metadata->SetStringField(TEXT("category"), Entry->MetaData.Category.ToString().Left(128));
    Metadata->SetStringField(TEXT("tooltip"), Entry->MetaData.ToolTip.ToString().Left(512));
    Metadata->SetStringField(TEXT("keywords"), Entry->MetaData.Keywords.ToString().Left(256));
    Metadata->SetBoolField(TEXT("call_in_editor"), Entry->MetaData.bCallInEditor);
    return Metadata;
}

static TSharedRef<FJsonObject> CallableMetadata(const FKismetUserDeclaredFunctionMetadata& Source, bool bCallInEditor)
{
    const TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
    Metadata->SetStringField(TEXT("category"), Source.Category.ToString().Left(128));
    Metadata->SetStringField(TEXT("tooltip"), Source.ToolTip.ToString().Left(512));
    Metadata->SetStringField(TEXT("keywords"), Source.Keywords.ToString().Left(256));
    Metadata->SetBoolField(TEXT("call_in_editor"), bCallInEditor);
    return Metadata;
}

static TSharedRef<FJsonObject> FunctionSignature(const UK2Node_FunctionEntry* Entry, const TArray<UK2Node_FunctionResult*>& Results)
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

static TSharedRef<FJsonObject> MacroSignature(UK2Node_Tunnel* Entry, UK2Node_Tunnel* Exit, bool bPure)
{
    const TSharedRef<FJsonObject> Signature = MakeShared<FJsonObject>();
    Signature->SetBoolField(TEXT("pure"), bPure);
    TArray<TSharedPtr<FJsonValue>> Parameters;
    auto Append = [&Parameters](const UK2Node_Tunnel* Node, const TCHAR* Direction)
    {
        if (Node == nullptr) return;
        for (const TSharedPtr<FUserPinInfo>& Pin : Node->UserDefinedPins)
        {
            if (!Pin.IsValid() || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
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
    Append(Exit, TEXT("output"));
    Signature->SetArrayField(TEXT("parameters"), Parameters);
    return Signature;
}

static TSharedRef<FJsonObject> CustomEventSignature(const UK2Node_CustomEvent* Event)
{
    const TSharedRef<FJsonObject> Signature = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Parameters;
    for (const TSharedPtr<FUserPinInfo>& Pin : Event->UserDefinedPins)
    {
        if (!Pin.IsValid()) continue;
        const TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
        Parameter->SetStringField(TEXT("name"), Pin->PinName.ToString());
        Parameter->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(Pin->PinType));
        if (!Pin->PinType.bIsReference)
        {
            Parameter->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(Pin->PinType, Pin->PinDefaultValue));
        }
        Parameters.Add(MakeShared<FJsonValueObject>(Parameter));
    }
    Signature->SetArrayField(TEXT("parameters"), Parameters);
    return Signature;
}

static TSharedRef<FJsonObject> VariableMetadata(const FBPVariableDescription& Variable)
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

static TSharedRef<FJsonObject> VariableReplication(UBlueprint* Blueprint, const FBPVariableDescription& Variable, const bool bMutable)
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

static FString VariableDefaultText(UBlueprint* Blueprint, const FBPVariableDescription& Variable)
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

static FString GraphKind(const UBlueprint* Blueprint, const UEdGraph* Graph)
{
    if (Blueprint->UbergraphPages.Contains(Graph)) return TEXT("event");
    if (Blueprint->FunctionGraphs.Contains(Graph)) return TEXT("function");
    if (Blueprint->MacroGraphs.Contains(Graph)) return TEXT("macro");
    if (Blueprint->DelegateSignatureGraphs.Contains(Graph)) return TEXT("delegate");
    return TEXT("other");
}

static void AddBlueprintGraphs(UBlueprint* Blueprint, const FString& OwnerPath, TArray<TPair<UEdGraph*, FString>>& OutGraphs)
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

static bool ReadPropertyNames(const FJsonObject& Arguments, TSet<FString>& OutNames, FUnrealMCPError& OutError)
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

static FString AddComponentDefaults(UActorComponent* Template, const TSet<FString>& RequestedProperties, const TSharedRef<FJsonObject>& Component)
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

static void AddClassDefaultFingerprint(UBlueprint* Blueprint, TArray<FString>& Fingerprint)
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

static bool AssetIsActorBlueprint(const FAssetData& Asset)
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


}
