#include "UnrealMCPAssetInspectionService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/GameInstance.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Tunnel.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMCPBlueprintInspectionSupport.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPK2TypeCodec.h"
#include "UnrealMCPProtocol.h"
#include "UnrealMCPPropertyCodec.h"
#include "UnrealMCPVersion.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace UnrealMCP::AssetInspectionPrivate
{
constexpr int32 DefaultPageSize = 10;
constexpr int32 MaximumPageSize = UnrealMCP::MaxAssetInspectPageSize;
constexpr int32 MaximumSelectorBytes = UnrealMCP::MaxAssetInspectSelectorBytes;
constexpr int32 CompleteGraphBytes = UnrealMCP::MaxAssetInspectCompleteGraphBytes;
constexpr int32 MaximumRootEntries = 100;
constexpr int32 MaximumBoundaryStubs = 128;

struct FRequest
{
    FString AssetPath;
    FString Selector;
    TArray<FString> Segments;
    int32 PageSize = DefaultPageSize;
    int32 PageIndex = 0;
    bool bVerbose = false;
    bool bAllowPartialGraph = false;
    bool bHasPaging = false;
    bool bHasPartialFlag = false;
};

struct FClassification
{
    FString Type;
    FString ParentType;
    FString StorageClass;
    FString RepresentedClass;
    bool bDeepBlueprint = false;
    bool bInterface = false;
    bool bMedia = false;
};

struct FGraphSelection
{
    UEdGraph* Graph = nullptr;
    UEdGraphNode* EventRoot = nullptr;
    FString Kind;
    FString Name;
    FString Selector;
};

bool HasOnlyFields(const FJsonObject& Object, std::initializer_list<const TCHAR*> Allowed)
{
    TSet<FString> Names;
    for (const TCHAR* Name : Allowed) Names.Add(Name);
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
    {
        if (!Names.Contains(Pair.Key)) return false;
    }
    return true;
}

bool IsUnreserved(uint8 Byte)
{
    return (Byte >= 'A' && Byte <= 'Z') || (Byte >= 'a' && Byte <= 'z')
        || (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '.' || Byte == '_' || Byte == '~';
}

int32 HexValue(TCHAR Character)
{
    if (Character >= '0' && Character <= '9') return Character - '0';
    if (Character >= 'A' && Character <= 'F') return Character - 'A' + 10;
    return INDEX_NONE;
}

FString EncodeSegment(const FString& Input)
{
    FTCHARToUTF8 Encoded(*Input);
    FString Result;
    static const TCHAR Digits[] = TEXT("0123456789ABCDEF");
    for (int32 Index = 0; Index < Encoded.Length(); ++Index)
    {
        const uint8 Byte = static_cast<uint8>(Encoded.Get()[Index]);
        if (IsUnreserved(Byte)) Result.AppendChar(static_cast<TCHAR>(Byte));
        else
        {
            Result.AppendChar('%');
            Result.AppendChar(Digits[(Byte >> 4) & 0x0F]);
            Result.AppendChar(Digits[Byte & 0x0F]);
        }
    }
    return Result;
}

bool DecodeSegment(const FString& Input, FString& Out)
{
    TArray<uint8> Bytes;
    if (Input.IsEmpty()) return false;
    for (int32 Index = 0; Index < Input.Len(); ++Index)
    {
        const TCHAR Character = Input[Index];
        if (Character == '%')
        {
            if (Index + 2 >= Input.Len()) return false;
            const int32 High = HexValue(Input[Index + 1]);
            const int32 Low = HexValue(Input[Index + 2]);
            if (High == INDEX_NONE || Low == INDEX_NONE) return false;
            Bytes.Add(static_cast<uint8>((High << 4) | Low));
            Index += 2;
        }
        else
        {
            if (Character > 0x7F || !IsUnreserved(static_cast<uint8>(Character))) return false;
            Bytes.Add(static_cast<uint8>(Character));
        }
    }
    if (Bytes.Contains(0)) return false;
    FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
    Out = FString(Converted.Length(), Converted.Get());
    if (Out.IsEmpty() || Out.Contains(TEXT("/")) || Out.Contains(TEXT("\\"))) return false;
    FTCHARToUTF8 RoundTrip(*Out);
    return RoundTrip.Length() == Bytes.Num()
        && FMemory::Memcmp(RoundTrip.Get(), Bytes.GetData(), Bytes.Num()) == 0;
}

bool NormalizeAssetPath(const FString& Input, FString& OutObjectPath)
{
    if (!Input.StartsWith(TEXT("/Game/")) || Input.StartsWith(TEXT("//"))
        || Input.Contains(TEXT("..")) || Input.Contains(TEXT("\\")) || Input.Contains(TEXT(":")))
    {
        return false;
    }
    const FString PackageName = FPackageName::ObjectPathToPackageName(Input);
    if (!FPackageName::IsValidLongPackageName(PackageName, true)) return false;
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    const FString Canonical = PackageName + TEXT(".") + AssetName;
    if (Input.Contains(TEXT(".")) && Input != Canonical) return false;
    if (!FPackageName::IsValidObjectPath(Canonical)) return false;
    OutObjectPath = Canonical;
    return true;
}

bool ReadInteger(const FJsonObject& Object, const TCHAR* Name, int32 Default, int32 Minimum, int32 Maximum,
    int32& Out, FUnrealMCPError& OutError)
{
    Out = Default;
    if (!Object.HasField(Name)) return true;
    double Number = 0.0;
    if (!Object.TryGetNumberField(Name, Number) || !FMath::IsFinite(Number)
        || !FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
        || Number < Minimum || Number > Maximum)
    {
        OutError = {TEXT("invalid_argument"), FString::Printf(TEXT("%s is outside the supported integer range"), Name)};
        return false;
    }
    Out = static_cast<int32>(Number);
    return true;
}

bool ReadBoolean(const FJsonObject& Object, const TCHAR* Name, bool Default, bool& Out, FUnrealMCPError& OutError)
{
    Out = Default;
    if (!Object.HasField(Name)) return true;
    if (!Object.TryGetBoolField(Name, Out))
    {
        OutError = {TEXT("invalid_argument"), FString::Printf(TEXT("%s must be boolean"), Name)};
        return false;
    }
    return true;
}

bool DecodeRequest(const TSharedPtr<FJsonObject>& Arguments, FRequest& Out, FUnrealMCPError& OutError)
{
    if (!Arguments.IsValid() || !HasOnlyFields(*Arguments,
        {TEXT("asset_path"), TEXT("selector"), TEXT("verbose"), TEXT("page_size"), TEXT("page_index"), TEXT("allow_partial_graph")}))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_inspect accepts only its six documented fields")};
        return false;
    }
    FString RawPath;
    if (!Arguments->TryGetStringField(TEXT("asset_path"), RawPath) || RawPath.Len() > 512
        || !NormalizeAssetPath(RawPath, Out.AssetPath))
    {
        OutError = {TEXT("invalid_argument"), TEXT("asset_path must be one exact /Game package or canonical object path")};
        return false;
    }
    if (Arguments->HasField(TEXT("selector")))
    {
        if (!Arguments->TryGetStringField(TEXT("selector"), Out.Selector)
            || Out.Selector.IsEmpty() || Out.Selector.Len() > MaximumSelectorBytes)
        {
            OutError = {TEXT("invalid_argument"), TEXT("selector must be one bounded canonical selector")};
            return false;
        }
        TArray<FString> EncodedSegments;
        Out.Selector.ParseIntoArray(EncodedSegments, TEXT("/"), false);
        if (EncodedSegments.IsEmpty())
        {
            OutError = {TEXT("invalid_argument"), TEXT("selector must contain non-empty path segments")};
            return false;
        }
        for (const FString& Encoded : EncodedSegments)
        {
            FString Decoded;
            if (!DecodeSegment(Encoded, Decoded) || EncodeSegment(Decoded) != Encoded)
            {
                OutError = {TEXT("invalid_argument"), TEXT("selector segments must use canonical uppercase UTF-8 percent encoding")};
                return false;
            }
            Out.Segments.Add(MoveTemp(Decoded));
        }
    }
    Out.bHasPaging = Arguments->HasField(TEXT("page_size")) || Arguments->HasField(TEXT("page_index"));
    Out.bHasPartialFlag = Arguments->HasField(TEXT("allow_partial_graph"));
    return ReadInteger(*Arguments, TEXT("page_size"), DefaultPageSize, 1, MaximumPageSize, Out.PageSize, OutError)
        && ReadInteger(*Arguments, TEXT("page_index"), 0, 0, 1000000, Out.PageIndex, OutError)
        && ReadBoolean(*Arguments, TEXT("verbose"), false, Out.bVerbose, OutError)
        && ReadBoolean(*Arguments, TEXT("allow_partial_graph"), false, Out.bAllowPartialGraph, OutError);
}

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

void AddBlueprintFingerprint(UBlueprint* Blueprint, TArray<FString>& Lines)
{
    using namespace UnrealMCP::BlueprintInspectionPrivate;
    if (Blueprint == nullptr) return;
    Lines.Add(TEXT("blueprint|") + Blueprint->GetPathName() + TEXT("|")
        + (Blueprint->ParentClass != nullptr ? Blueprint->ParentClass->GetPathName() : FString())
        + TEXT("|") + LexToString(static_cast<int32>(Blueprint->BlueprintType)));
    AddClassDefaultFingerprint(Blueprint, Lines);
    for (const FBPVariableDescription& Variable : Blueprint->NewVariables)
    {
        Lines.Add(TEXT("variable|") + GuidString(Variable.VarGuid) + TEXT("|") + Variable.VarName.ToString()
            + TEXT("|") + Variable.VarType.PinCategory.ToString() + TEXT("|") + Variable.DefaultValue);
    }
    TArray<UEdGraph*> Graphs;
    Graphs.Append(Blueprint->UbergraphPages);
    Graphs.Append(Blueprint->FunctionGraphs);
    Graphs.Append(Blueprint->MacroGraphs);
    for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces) Graphs.Append(Interface.Graphs);
    for (UEdGraph* Graph : Graphs)
    {
        if (Graph == nullptr) continue;
        Lines.Add(TEXT("graph|") + GuidString(Graph->GraphGuid) + TEXT("|") + Graph->GetName());
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr) continue;
            const FString NodeId = GuidString(Node->NodeGuid);
            Lines.Add(TEXT("node|") + NodeId + TEXT("|") + Node->GetClass()->GetPathName()
                + TEXT("|") + LexToString(Node->NodePosX) + TEXT("|") + LexToString(Node->NodePosY));
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (!IsStructuralGraphPin(Node, Pin)) continue;
                const FString PinId = GuidString(Pin->PinId);
                Lines.Add(TEXT("pin|") + NodeId + TEXT("|") + PinId + TEXT("|") + Pin->PinName.ToString()
                    + TEXT("|") + Pin->PinType.PinCategory.ToString() + TEXT("|") + Pin->DefaultValue
                    + TEXT("|") + (Pin->DefaultObject != nullptr ? Pin->DefaultObject->GetPathName() : FString())
                    + TEXT("|") + Pin->DefaultTextValue.ToString());
                if (Pin->Direction == EGPD_Output)
                {
                    for (UEdGraphPin* Linked : Pin->LinkedTo)
                    {
                        if (Linked != nullptr) Lines.Add(TEXT("link|") + PinId + TEXT("|") + GuidString(Linked->PinId));
                    }
                }
            }
        }
    }
}

FString BuildSnapshot(UObject* AssetObject, UBlueprint* Blueprint)
{
    TArray<FString> Lines;
    Lines.Add(TEXT("asset|") + AssetObject->GetPathName() + TEXT("|") + AssetObject->GetClass()->GetPathName());
    if (Blueprint != nullptr) AddBlueprintFingerprint(Blueprint, Lines);
    else if (UPackage* Package = AssetObject->GetOutermost())
    {
        Lines.Add(TEXT("package|") + Package->GetName() + TEXT("|") + LexToString(Package->IsDirty()));
    }
    return HashLines(MoveTemp(Lines));
}

FString BuildStableSnapshot(UObject* AssetObject, UBlueprint* Blueprint)
{
    if (Blueprint != nullptr)
    {
        // Blueprint mutation commands still compare against the established structural
        // fingerprint. Keep that internal precondition contract while replacing the
        // model-facing inspector and its reconstruction-oriented records.
        FUnrealMCPBlueprintInspector Inspector;
        const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
        Arguments->SetStringField(TEXT("mode"), TEXT("inspect"));
        Arguments->SetStringField(TEXT("asset_path"), AssetObject->GetPathName());
        Arguments->SetNumberField(TEXT("page_size"), 1);
        TSharedPtr<FJsonObject> Result;
        FUnrealMCPError Error;
        if (Inspector.Execute(Arguments, Result, Error) && Result.IsValid())
        {
            FString Snapshot;
            if (Result->TryGetStringField(TEXT("snapshot_id"), Snapshot) && !Snapshot.IsEmpty())
            {
                return Snapshot;
            }
        }
    }
    return BuildSnapshot(AssetObject, Blueprint);
}

bool IsMediaClass(const FString& ClassPath)
{
    static const TArray<FString> Markers = {
        TEXT("Texture"), TEXT("StaticMesh"), TEXT("SkeletalMesh"), TEXT("SoundWave"),
        TEXT("MediaSource"), TEXT("MediaTexture"), TEXT("AnimSequence"), TEXT("AnimMontage"),
        TEXT("FontFace"), TEXT("GeometryCache")};
    for (const FString& Marker : Markers)
    {
        if (ClassPath.Contains(Marker)) return true;
    }
    return false;
}

FClassification Classify(UObject* AssetObject, UBlueprint* Blueprint)
{
    FClassification Result;
    Result.StorageClass = AssetObject->GetClass()->GetPathName();
    if (Blueprint == nullptr)
    {
        Result.Type = TEXT("asset");
        Result.bMedia = IsMediaClass(Result.StorageClass);
        return Result;
    }
    UClass* Represented = Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass : Blueprint->ParentClass;
    Result.RepresentedClass = Represented != nullptr ? Represented->GetPathName() : FString();
    Result.ParentType = Blueprint->ParentClass != nullptr ? Blueprint->ParentClass->GetPathName() : FString();
    if (Blueprint->BlueprintType == BPTYPE_Interface)
    {
        Result.Type = TEXT("interface_blueprint");
        Result.bDeepBlueprint = true;
        Result.bInterface = true;
    }
    else if (Represented != nullptr && Represented->IsChildOf(AGameMode::StaticClass()))
    {
        Result.Type = TEXT("game_mode_blueprint"); Result.bDeepBlueprint = true;
    }
    else if (Represented != nullptr && Represented->IsChildOf(AGameModeBase::StaticClass()))
    {
        Result.Type = TEXT("game_mode_base_blueprint"); Result.bDeepBlueprint = true;
    }
    else if (Represented != nullptr && Represented->IsChildOf(AGameState::StaticClass()))
    {
        Result.Type = TEXT("game_state_blueprint"); Result.bDeepBlueprint = true;
    }
    else if (Represented != nullptr && Represented->IsChildOf(AGameStateBase::StaticClass()))
    {
        Result.Type = TEXT("game_state_base_blueprint"); Result.bDeepBlueprint = true;
    }
    else if (Represented != nullptr && Represented->IsChildOf(APlayerController::StaticClass()))
    {
        Result.Type = TEXT("player_controller_blueprint"); Result.bDeepBlueprint = true;
    }
    else if (Represented != nullptr && Represented->IsChildOf(APlayerState::StaticClass()))
    {
        Result.Type = TEXT("player_state_blueprint"); Result.bDeepBlueprint = true;
    }
    else if (Represented != nullptr && Represented->IsChildOf(UGameInstance::StaticClass()))
    {
        Result.Type = TEXT("game_instance_blueprint"); Result.bDeepBlueprint = true;
    }
    else if (Represented != nullptr && Represented->IsChildOf(UActorComponent::StaticClass()))
    {
        Result.Type = TEXT("actor_component_blueprint"); Result.bDeepBlueprint = true;
    }
    else if (Represented != nullptr && Represented->IsChildOf(AActor::StaticClass()))
    {
        Result.Type = TEXT("actor_blueprint"); Result.bDeepBlueprint = true;
    }
    else
    {
        Result.Type = TEXT("unsupported_blueprint");
    }
    return Result;
}

TSharedRef<FJsonObject> BaseResult(const FString& AssetPath, const FString& Snapshot,
    const FClassification& Classification)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("format"), TEXT("yaml"));
    Result->SetNumberField(TEXT("schema_version"), 1);
    Result->SetStringField(TEXT("snapshot_id"), Snapshot);
    const TSharedRef<FJsonObject> Asset = MakeShared<FJsonObject>();
    Asset->SetStringField(TEXT("path"), AssetPath);
    Asset->SetStringField(TEXT("type"), Classification.Type);
    Asset->SetStringField(TEXT("storage_class"), Classification.StorageClass);
    if (!Classification.RepresentedClass.IsEmpty()) Asset->SetStringField(TEXT("represented_class"), Classification.RepresentedClass);
    if (!Classification.ParentType.IsEmpty()) Asset->SetStringField(TEXT("parent_type"), Classification.ParentType);
    Result->SetObjectField(TEXT("asset"), Asset);
    return Result;
}

TSharedPtr<FJsonValue> ScalarPropertyValue(UObject* Object, const TCHAR* PropertyName)
{
    if (Object == nullptr) return nullptr;
    FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
    if (Property == nullptr || Property->ArrayDim != 1
        || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)) return nullptr;
    const void* Address = Property->ContainerPtrToValuePtr<void>(Object);
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
        return MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Address));
    if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
    {
        if (Numeric->IsFloatingPoint()) return MakeShared<FJsonValueNumber>(Numeric->GetFloatingPointPropertyValue(Address));
        return MakeShared<FJsonValueNumber>(static_cast<double>(Numeric->GetSignedIntPropertyValue(Address)));
    }
    if (const FNameProperty* Name = CastField<FNameProperty>(Property))
        return MakeShared<FJsonValueString>(Name->GetPropertyValue(Address).ToString());
    if (const FStrProperty* String = CastField<FStrProperty>(Property))
        return MakeShared<FJsonValueString>(String->GetPropertyValue(Address));
    if (const FTextProperty* Text = CastField<FTextProperty>(Property))
        return MakeShared<FJsonValueString>(Text->GetPropertyValue(Address).ToString());
    if (const FClassProperty* Class = CastField<FClassProperty>(Property))
    {
        const UClass* Value = Cast<UClass>(Class->GetObjectPropertyValue(Address));
        return MakeShared<FJsonValueString>(Value != nullptr ? Value->GetPathName() : FString());
    }
    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        const UObject* Value = ObjectProperty->GetObjectPropertyValue(Address);
        return MakeShared<FJsonValueString>(Value != nullptr ? Value->GetPathName() : FString());
    }
    FString TextValue;
    Property->ExportText_Direct(TextValue, Address, nullptr, Object, PPF_None);
    if (TextValue.Len() <= 4096) return MakeShared<FJsonValueString>(TextValue);
    return nullptr;
}

void AddScalar(UObject* Object, const TSharedRef<FJsonObject>& Target, const TCHAR* OutputName, const TCHAR* PropertyName)
{
    if (TSharedPtr<FJsonValue> Value = ScalarPropertyValue(Object, PropertyName)) Target->SetField(OutputName, Value);
}

int32 CollectionCount(UObject* Object, const TCHAR* PropertyName)
{
    if (Object == nullptr) return 0;
    FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
    const void* Address = Property != nullptr ? Property->ContainerPtrToValuePtr<void>(Object) : nullptr;
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property)) return FScriptArrayHelper(Array, Address).Num();
    if (const FSetProperty* Set = CastField<FSetProperty>(Property)) return FScriptSetHelper(Set, Address).Num();
    if (const FMapProperty* Map = CastField<FMapProperty>(Property)) return FScriptMapHelper(Map, Address).Num();
    return 0;
}

TSharedRef<FJsonObject> CollectionSummary(const TCHAR* Kind, const TCHAR* ItemType, int32 Count, const FString& Selector)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("kind"), Kind);
    Result->SetStringField(TEXT("item_type"), ItemType);
    Result->SetNumberField(TEXT("count"), Count);
    Result->SetStringField(TEXT("selector"), Selector);
    return Result;
}

void AddSelector(TArray<TSharedPtr<FJsonValue>>& Selectors, const FString& Selector)
{
    if (!Selectors.ContainsByPredicate([&Selector](const TSharedPtr<FJsonValue>& Value)
        { FString Existing; return Value.IsValid() && Value->TryGetString(Existing) && Existing == Selector; }))
    {
        Selectors.Add(MakeShared<FJsonValueString>(Selector));
    }
}

FString EventName(UEdGraphNode* Node)
{
    if (const UK2Node_CustomEvent* Custom = Cast<UK2Node_CustomEvent>(Node)) return Custom->CustomFunctionName.ToString();
    if (const UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
    {
        const FName MemberName = Event->EventReference.GetMemberName();
        return !MemberName.IsNone() ? MemberName.ToString() : Event->GetNodeTitle(ENodeTitleType::ListView).ToString();
    }
    return FString();
}

TSharedRef<FJsonObject> SignatureFromGraph(UEdGraph* Graph)
{
    const TSharedRef<FJsonObject> Signature = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Inputs;
    TArray<TSharedPtr<FJsonValue>> Outputs;
    UK2Node_FunctionEntry* Entry = Graph != nullptr ? Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Graph)) : nullptr;
    TArray<UK2Node_FunctionResult*> Results;
    if (Graph != nullptr) Graph->GetNodesOfClass(Results);
    auto Append = [](const UK2Node_EditablePinBase* Node, TArray<TSharedPtr<FJsonValue>>& Destination)
    {
        if (Node == nullptr) return;
        for (const TSharedPtr<FUserPinInfo>& Pin : Node->UserDefinedPins)
        {
            if (!Pin.IsValid()) continue;
            const TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
            Parameter->SetStringField(TEXT("name"), Pin->PinName.ToString());
            Parameter->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(Pin->PinType));
            Parameter->SetStringField(TEXT("passing"), Pin->PinType.bIsReference
                ? (Pin->PinType.bIsConst ? TEXT("const_reference") : TEXT("reference")) : TEXT("value"));
            if (!Pin->PinType.bIsReference)
                Parameter->SetObjectField(TEXT("default"), UnrealMCP::K2TypeCodec::EncodeDefault(Pin->PinType, Pin->PinDefaultValue));
            Destination.Add(MakeShared<FJsonValueObject>(Parameter));
        }
    };
    Append(Entry, Inputs);
    if (!Results.IsEmpty()) Append(Results[0], Outputs);
    Signature->SetArrayField(TEXT("inputs"), Inputs);
    Signature->SetArrayField(TEXT("outputs"), Outputs);
    if (Entry != nullptr)
    {
        Signature->SetBoolField(TEXT("pure"), (Entry->GetFunctionFlags() & FUNC_BlueprintPure) != 0);
        Signature->SetBoolField(TEXT("const"), (Entry->GetFunctionFlags() & FUNC_Const) != 0);
    }
    return Signature;
}

TSharedRef<FJsonObject> MacroSignature(UEdGraph* Graph)
{
    const TSharedRef<FJsonObject> Signature = MakeShared<FJsonObject>();
    UK2Node_Tunnel* Entry = nullptr;
    UK2Node_Tunnel* Exit = nullptr;
    bool bPure = false;
    FKismetEditorUtilities::GetInformationOnMacro(Graph, Entry, Exit, bPure);
    TArray<TSharedPtr<FJsonValue>> Inputs;
    TArray<TSharedPtr<FJsonValue>> Outputs;
    auto Append = [](const UK2Node_Tunnel* Node, TArray<TSharedPtr<FJsonValue>>& Destination)
    {
        if (Node == nullptr) return;
        for (const TSharedPtr<FUserPinInfo>& Pin : Node->UserDefinedPins)
        {
            if (!Pin.IsValid() || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
            const TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
            Parameter->SetStringField(TEXT("name"), Pin->PinName.ToString());
            Parameter->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(Pin->PinType));
            Destination.Add(MakeShared<FJsonValueObject>(Parameter));
        }
    };
    Append(Entry, Inputs);
    Append(Exit, Outputs);
    Signature->SetBoolField(TEXT("pure"), bPure);
    Signature->SetArrayField(TEXT("inputs"), Inputs);
    Signature->SetArrayField(TEXT("outputs"), Outputs);
    return Signature;
}

void AddFamilySemantics(UBlueprint* Blueprint, const FClassification& Classification,
    const TSharedRef<FJsonObject>& Result, TArray<TSharedPtr<FJsonValue>>& Selectors)
{
    UObject* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
        ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
    if (Defaults == nullptr) return;
    if (AActor* Actor = Cast<AActor>(Defaults))
    {
        const TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
        const FString Presence = Classification.Type.StartsWith(TEXT("game_mode")) ? TEXT("server_only")
            : Classification.Type.StartsWith(TEXT("game_state")) ? TEXT("server_and_clients")
            : Classification.Type == TEXT("player_controller_blueprint") ? TEXT("server_and_owning_client")
            : Classification.Type == TEXT("player_state_blueprint") ? TEXT("server_and_all_clients")
            : TEXT("world");
        Block->SetStringField(TEXT("world_presence"), Presence);
        Block->SetBoolField(TEXT("placeable"), !Blueprint->GeneratedClass->HasAnyClassFlags(CLASS_NotPlaceable));
        Block->SetObjectField(TEXT("tags"), CollectionSummary(TEXT("array"), TEXT("name"),
            Actor->Tags.Num(), TEXT("properties/actor/tags")));
        AddSelector(Selectors, TEXT("properties"));
        const TSharedRef<FJsonObject> Replication = MakeShared<FJsonObject>();
        Replication->SetBoolField(TEXT("enabled"), Actor->GetIsReplicated());
        AddScalar(Actor, Replication, TEXT("always_relevant"), TEXT("bAlwaysRelevant"));
        AddScalar(Actor, Replication, TEXT("movement"), TEXT("bReplicateMovement"));
        AddScalar(Actor, Replication, TEXT("net_load_on_client"), TEXT("bNetLoadOnClient"));
        AddScalar(Actor, Replication, TEXT("priority"), TEXT("NetPriority"));
        AddScalar(Actor, Replication, TEXT("update_frequency_hz"), TEXT("NetUpdateFrequency"));
        Block->SetObjectField(TEXT("replication"), Replication);
        const TSharedRef<FJsonObject> Tick = MakeShared<FJsonObject>();
        Tick->SetBoolField(TEXT("can_ever_tick"), Actor->PrimaryActorTick.bCanEverTick);
        if (Actor->PrimaryActorTick.bCanEverTick)
        {
            Tick->SetBoolField(TEXT("start_enabled"), Actor->PrimaryActorTick.bStartWithTickEnabled);
            Tick->SetNumberField(TEXT("interval_seconds"), Actor->PrimaryActorTick.TickInterval);
            Tick->SetBoolField(TEXT("even_when_paused"), Actor->PrimaryActorTick.bTickEvenWhenPaused);
            Tick->SetBoolField(TEXT("dedicated_server"), Actor->PrimaryActorTick.bAllowTickOnDedicatedServer);
        }
        Block->SetObjectField(TEXT("tick"), Tick);
        Result->SetObjectField(TEXT("actor"), Block);
    }
    if (Classification.Type == TEXT("game_instance_blueprint"))
    {
        const TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
        Block->SetStringField(TEXT("lifetime"), TEXT("game_instance_session"));
        Block->SetBoolField(TEXT("persists_across_level_travel"), true);
        Block->SetStringField(TEXT("network_scope"), TEXT("local_game_instance"));
        Block->SetBoolField(TEXT("replicated"), false);
        Result->SetObjectField(TEXT("game_instance"), Block);
    }
    if (Classification.Type.StartsWith(TEXT("game_mode")))
    {
        const TSharedRef<FJsonObject> Base = MakeShared<FJsonObject>();
        const TSharedRef<FJsonObject> Classes = MakeShared<FJsonObject>();
        AddScalar(Defaults, Classes, TEXT("game_session"), TEXT("GameSessionClass"));
        AddScalar(Defaults, Classes, TEXT("game_state"), TEXT("GameStateClass"));
        AddScalar(Defaults, Classes, TEXT("player_controller"), TEXT("PlayerControllerClass"));
        AddScalar(Defaults, Classes, TEXT("player_state"), TEXT("PlayerStateClass"));
        AddScalar(Defaults, Classes, TEXT("hud"), TEXT("HUDClass"));
        AddScalar(Defaults, Classes, TEXT("default_pawn"), TEXT("DefaultPawnClass"));
        AddScalar(Defaults, Classes, TEXT("spectator_pawn"), TEXT("SpectatorClass"));
        Base->SetObjectField(TEXT("classes"), Classes);
        AddScalar(Defaults, Base, TEXT("seamless_travel"), TEXT("bUseSeamlessTravel"));
        AddScalar(Defaults, Base, TEXT("start_players_as_spectators"), TEXT("bStartPlayersAsSpectators"));
        AddScalar(Defaults, Base, TEXT("pauseable"), TEXT("bPauseable"));
        AddScalar(Defaults, Base, TEXT("default_player_name"), TEXT("DefaultPlayerName"));
        Result->SetObjectField(TEXT("game_mode_base"), Base);
        if (Classification.Type == TEXT("game_mode_blueprint"))
        {
            const TSharedRef<FJsonObject> Mode = MakeShared<FJsonObject>();
            AddScalar(Defaults, Mode, TEXT("delayed_start"), TEXT("bDelayedStart"));
            AddScalar(Defaults, Mode, TEXT("minimum_respawn_delay_seconds"), TEXT("MinRespawnDelay"));
            AddScalar(Defaults, Mode, TEXT("inactive_player_state_lifespan_seconds"), TEXT("InactivePlayerStateLifeSpan"));
            AddScalar(Defaults, Mode, TEXT("maximum_inactive_player_states"), TEXT("MaxInactivePlayers"));
            AddScalar(Defaults, Mode, TEXT("dedicated_server_replays"), TEXT("bHandleDedicatedServerReplays"));
            Result->SetObjectField(TEXT("game_mode"), Mode);
        }
    }
    if (Classification.Type.StartsWith(TEXT("game_state")))
    {
        const TSharedRef<FJsonObject> Base = MakeShared<FJsonObject>();
        AddScalar(Defaults, Base, TEXT("server_world_time_update_frequency_seconds"), TEXT("ServerWorldTimeSecondsUpdateFrequency"));
        const TSharedRef<FJsonObject> Contract = MakeShared<FJsonObject>();
        Contract->SetStringField(TEXT("authority_game_mode"), TEXT("server_only"));
        Contract->SetStringField(TEXT("player_states"), TEXT("maintained_on_server_and_clients"));
        Contract->SetStringField(TEXT("begun_play"), TEXT("replicated_from_authority"));
        Base->SetObjectField(TEXT("runtime_contract"), Contract);
        Result->SetObjectField(TEXT("game_state_base"), Base);
        if (Classification.Type == TEXT("game_state_blueprint"))
        {
            const TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
            const TSharedRef<FJsonObject> StateContract = MakeShared<FJsonObject>();
            StateContract->SetStringField(TEXT("match_state"), TEXT("replicated_from_authority"));
            StateContract->SetStringField(TEXT("previous_match_state"), TEXT("local_transition_context"));
            StateContract->SetStringField(TEXT("elapsed_time_seconds"), TEXT("replicated_from_authority"));
            State->SetObjectField(TEXT("runtime_contract"), StateContract);
            Result->SetObjectField(TEXT("game_state"), State);
        }
    }
    if (Classification.Type == TEXT("player_controller_blueprint"))
    {
        const TSharedRef<FJsonObject> Controller = MakeShared<FJsonObject>();
        AddScalar(Defaults, Controller, TEXT("attach_to_pawn"), TEXT("bAttachToPawn"));
        Controller->SetStringField(TEXT("possession_authority"), TEXT("server"));
        Result->SetObjectField(TEXT("controller"), Controller);
        const TSharedRef<FJsonObject> Player = MakeShared<FJsonObject>();
        AddScalar(Defaults, Player, TEXT("camera_manager_class"), TEXT("PlayerCameraManagerClass"));
        AddScalar(Defaults, Player, TEXT("auto_manage_camera_target"), TEXT("bAutoManageActiveCameraTarget"));
        AddScalar(Defaults, Player, TEXT("show_mouse_cursor"), TEXT("bShowMouseCursor"));
        AddScalar(Defaults, Player, TEXT("click_events"), TEXT("bEnableClickEvents"));
        AddScalar(Defaults, Player, TEXT("touch_events"), TEXT("bEnableTouchEvents"));
        const int32 ClickKeys = CollectionCount(Defaults, TEXT("ClickEventKeys"));
        Player->SetObjectField(TEXT("click_keys"), CollectionSummary(TEXT("array"), TEXT("key"), ClickKeys,
            TEXT("properties/player_controller/click_keys")));
        AddSelector(Selectors, TEXT("properties"));
        Result->SetObjectField(TEXT("player_controller"), Player);
    }
    if (Classification.Type == TEXT("player_state_blueprint"))
    {
        const TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
        AddScalar(Defaults, State, TEXT("update_replicated_ping"), TEXT("bShouldUpdateReplicatedPing"));
        AddScalar(Defaults, State, TEXT("use_custom_player_names"), TEXT("bUseCustomPlayerNames"));
        AddScalar(Defaults, State, TEXT("engine_message_class"), TEXT("EngineMessageClass"));
        AddScalar(Defaults, State, TEXT("session_name"), TEXT("SessionName"));
        const TSharedRef<FJsonObject> Contract = MakeShared<FJsonObject>();
        for (const TCHAR* Name : {TEXT("score"), TEXT("player_id"), TEXT("player_name"), TEXT("unique_net_id"),
            TEXT("compressed_ping"), TEXT("spectator"), TEXT("bot"), TEXT("inactive"), TEXT("start_time_seconds")})
        {
            Contract->SetStringField(Name, TEXT("authority_server_replicated_to_all_clients"));
        }
        State->SetObjectField(TEXT("runtime_contract"), Contract);
        const TSharedRef<FJsonObject> Transfer = MakeShared<FJsonObject>();
        Transfer->SetStringField(TEXT("seamless_travel"), TEXT("CopyProperties"));
        Transfer->SetStringField(TEXT("reconnection"), TEXT("OverrideWith"));
        State->SetObjectField(TEXT("transfer_contract"), Transfer);
        Result->SetObjectField(TEXT("player_state"), State);
    }
    if (Classification.Type == TEXT("actor_component_blueprint"))
    {
        if (UActorComponent* Component = Cast<UActorComponent>(Defaults))
        {
            const TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
            Block->SetBoolField(TEXT("scene_component"), Component->IsA<USceneComponent>());
            AddScalar(Component, Block, TEXT("auto_activate"), TEXT("bAutoActivate"));
            AddScalar(Component, Block, TEXT("editor_only"), TEXT("bIsEditorOnly"));
            Block->SetObjectField(TEXT("tags"), CollectionSummary(TEXT("array"), TEXT("name"),
                Component->ComponentTags.Num(), TEXT("properties/component/tags")));
            AddSelector(Selectors, TEXT("properties"));
            const TSharedRef<FJsonObject> Replication = MakeShared<FJsonObject>();
            Replication->SetBoolField(TEXT("enabled"), Component->GetIsReplicated());
            Replication->SetBoolField(TEXT("requires_replicating_owner"), true);
            AddScalar(Component, Replication, TEXT("registered_subobject_list"), TEXT("bReplicateUsingRegisteredSubObjectList"));
            Block->SetObjectField(TEXT("replication"), Replication);
            const TSharedRef<FJsonObject> Tick = MakeShared<FJsonObject>();
            Tick->SetBoolField(TEXT("can_ever_tick"), Component->PrimaryComponentTick.bCanEverTick);
            if (Component->PrimaryComponentTick.bCanEverTick)
            {
                Tick->SetBoolField(TEXT("start_enabled"), Component->PrimaryComponentTick.bStartWithTickEnabled);
                Tick->SetNumberField(TEXT("interval_seconds"), Component->PrimaryComponentTick.TickInterval);
                Tick->SetBoolField(TEXT("dedicated_server"), Component->PrimaryComponentTick.bAllowTickOnDedicatedServer);
            }
            Block->SetObjectField(TEXT("tick"), Tick);
            Result->SetObjectField(TEXT("component"), Block);
        }
    }
}

void AddRootDeclarations(UBlueprint* Blueprint, const FClassification& Classification,
    const TSharedRef<FJsonObject>& Result, TArray<TSharedPtr<FJsonValue>>& Selectors)
{
    TArray<FBPVariableDescription> Variables = Blueprint->NewVariables;
    Variables.Sort([](const FBPVariableDescription& Left, const FBPVariableDescription& Right)
        { return Left.VarName.LexicalLess(Right.VarName); });
    TArray<TSharedPtr<FJsonValue>> VariableValues;
    for (const FBPVariableDescription& Variable : Variables)
    {
        if (VariableValues.Num() >= MaximumRootEntries) break;
        const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("name"), Variable.VarName.ToString());
        Value->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(Variable.VarType));
        const TSharedRef<FJsonObject> Default = UnrealMCP::K2TypeCodec::EncodeDefault(Variable.VarType, Variable.DefaultValue);
        FString Kind;
        Default->TryGetStringField(TEXT("kind"), Kind);
        if (Kind == TEXT("array") || Kind == TEXT("set") || Kind == TEXT("map"))
        {
            const TCHAR* FieldName = Kind == TEXT("map") ? TEXT("entries") : TEXT("items");
            const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
            const int32 Count = Default->TryGetArrayField(FieldName, Items) && Items != nullptr ? Items->Num() : 0;
            Value->SetObjectField(TEXT("value"), CollectionSummary(*Kind, TEXT("k2_value"), Count,
                TEXT("properties/variables/") + EncodeSegment(Variable.VarName.ToString())));
            AddSelector(Selectors, TEXT("properties"));
        }
        else Value->SetObjectField(TEXT("value"), Default);
        VariableValues.Add(MakeShared<FJsonValueObject>(Value));
    }
    if (!VariableValues.IsEmpty())
    {
        Result->SetArrayField(TEXT("variables"), VariableValues);
        AddSelector(Selectors, TEXT("variables"));
    }

    TArray<UEdGraph*> EventGraphs = Blueprint->UbergraphPages;
    EventGraphs.Sort([](const UEdGraph& Left, const UEdGraph& Right) { return Left.GetName() < Right.GetName(); });
    TArray<TSharedPtr<FJsonValue>> EventGraphValues;
    TArray<TSharedPtr<FJsonValue>> EventValues;
    for (UEdGraph* Graph : EventGraphs)
    {
        if (Graph == nullptr || !FBlueprintEditorUtils::IsEventGraph(Graph)) continue;
        TArray<FString> Names;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            const FString Name = EventName(Node);
            if (!Name.IsEmpty()) Names.AddUnique(Name);
        }
        Names.Sort();
        const TSharedRef<FJsonObject> GraphValue = MakeShared<FJsonObject>();
        GraphValue->SetStringField(TEXT("name"), Graph->GetName());
        GraphValue->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
        GraphValue->SetStringField(TEXT("selector"), TEXT("event_graphs/") + EncodeSegment(Graph->GetName()));
        TArray<TSharedPtr<FJsonValue>> NamesJson;
        for (const FString& Name : Names)
        {
            NamesJson.Add(MakeShared<FJsonValueString>(Name));
            const TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
            Event->SetStringField(TEXT("graph"), Graph->GetName());
            Event->SetStringField(TEXT("name"), Name);
            Event->SetStringField(TEXT("selector"), TEXT("events/") + EncodeSegment(Graph->GetName())
                + TEXT("/") + EncodeSegment(Name));
            EventValues.Add(MakeShared<FJsonValueObject>(Event));
        }
        GraphValue->SetArrayField(TEXT("events"), NamesJson);
        EventGraphValues.Add(MakeShared<FJsonValueObject>(GraphValue));
    }
    if (!EventGraphValues.IsEmpty() && !Classification.bInterface)
    {
        Result->SetArrayField(TEXT("event_graphs"), EventGraphValues);
        AddSelector(Selectors, TEXT("event_graphs"));
    }
    if (!EventValues.IsEmpty() && !Classification.bInterface)
    {
        Result->SetArrayField(TEXT("events"), EventValues);
        AddSelector(Selectors, TEXT("events"));
    }

    TArray<UEdGraph*> Functions = Blueprint->FunctionGraphs;
    if (Classification.bInterface)
    {
        for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces) Functions.Append(Interface.Graphs);
    }
    Functions.Sort([](const UEdGraph& Left, const UEdGraph& Right) { return Left.GetName() < Right.GetName(); });
    TArray<TSharedPtr<FJsonValue>> FunctionValues;
    for (UEdGraph* Graph : Functions)
    {
        if (Graph == nullptr || FunctionValues.Num() >= MaximumRootEntries) continue;
        const TSharedRef<FJsonObject> Function = MakeShared<FJsonObject>();
        Function->SetStringField(TEXT("name"), Graph->GetName());
        Function->SetObjectField(TEXT("signature"), SignatureFromGraph(Graph));
        Function->SetStringField(TEXT("selector"), TEXT("functions/") + EncodeSegment(Graph->GetName()));
        if (Classification.bInterface) Function->SetStringField(TEXT("dispatch"), TEXT("interface_message"));
        FunctionValues.Add(MakeShared<FJsonValueObject>(Function));
    }
    if (!FunctionValues.IsEmpty())
    {
        Result->SetArrayField(TEXT("functions"), FunctionValues);
        AddSelector(Selectors, TEXT("functions"));
    }

    if (!Classification.bInterface)
    {
        TArray<UEdGraph*> Macros = Blueprint->MacroGraphs;
        Macros.Sort([](const UEdGraph& Left, const UEdGraph& Right) { return Left.GetName() < Right.GetName(); });
        TArray<TSharedPtr<FJsonValue>> MacroValues;
        for (UEdGraph* Graph : Macros)
        {
            if (Graph == nullptr || MacroValues.Num() >= MaximumRootEntries) continue;
            const TSharedRef<FJsonObject> Macro = MakeShared<FJsonObject>();
            Macro->SetStringField(TEXT("name"), Graph->GetName());
            Macro->SetObjectField(TEXT("signature"), MacroSignature(Graph));
            Macro->SetStringField(TEXT("selector"), TEXT("macros/") + EncodeSegment(Graph->GetName()));
            MacroValues.Add(MakeShared<FJsonValueObject>(Macro));
        }
        if (!MacroValues.IsEmpty())
        {
            Result->SetArrayField(TEXT("macros"), MacroValues);
            AddSelector(Selectors, TEXT("macros"));
        }
    }

    if (!Blueprint->ImplementedInterfaces.IsEmpty() && !Classification.bInterface)
    {
        Result->SetObjectField(TEXT("implemented_interfaces"), CollectionSummary(TEXT("array"), TEXT("class"),
            Blueprint->ImplementedInterfaces.Num(), TEXT("properties/implemented_interfaces")));
        AddSelector(Selectors, TEXT("properties"));
    }

    if (!Classification.bInterface && Blueprint->SimpleConstructionScript != nullptr)
    {
        const int32 Count = Blueprint->SimpleConstructionScript->GetAllNodes().Num();
        if (Count > 0)
        {
            Result->SetObjectField(TEXT("components"), CollectionSummary(TEXT("array"), TEXT("actor_component"),
                Count, TEXT("components")));
            AddSelector(Selectors, TEXT("components"));
        }
    }
    if (Classification.bInterface)
    {
        const TSharedRef<FJsonObject> Interface = MakeShared<FJsonObject>();
        if (!Blueprint->BlueprintDescription.IsEmpty()) Interface->SetStringField(TEXT("description"), Blueprint->BlueprintDescription.Left(512));
        Interface->SetNumberField(TEXT("function_count"), FunctionValues.Num());
        Result->SetObjectField(TEXT("interface"), Interface);
    }
}

bool FindGraphSelection(UBlueprint* Blueprint, const FRequest& Request, const FClassification& Classification,
    FGraphSelection& Out, FUnrealMCPError& OutError)
{
    if (Request.Segments.IsEmpty()) return false;
    const FString& Namespace = Request.Segments[0];
    if (Namespace == TEXT("event_graphs") && Request.Segments.Num() == 2)
    {
        Out.Kind = TEXT("event_graph"); Out.Name = Request.Segments[1];
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
            if (Graph != nullptr && Graph->GetName() == Out.Name) { Out.Graph = Graph; break; }
    }
    else if (Namespace == TEXT("events") && Request.Segments.Num() == 3)
    {
        Out.Kind = TEXT("event"); Out.Name = Request.Segments[2];
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
            if (Graph != nullptr && Graph->GetName() == Request.Segments[1]) { Out.Graph = Graph; break; }
        if (Out.Graph != nullptr)
        {
            for (UEdGraphNode* Node : Out.Graph->Nodes)
                if (EventName(Node) == Out.Name) { Out.EventRoot = Node; break; }
        }
    }
    else if (Namespace == TEXT("functions") && Request.Segments.Num() == 2)
    {
        Out.Kind = Classification.bInterface ? TEXT("interface_function") : TEXT("function"); Out.Name = Request.Segments[1];
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
            if (Graph != nullptr && Graph->GetName() == Out.Name) { Out.Graph = Graph; break; }
    }
    else if (Namespace == TEXT("macros") && Request.Segments.Num() == 2 && !Classification.bInterface)
    {
        Out.Kind = TEXT("macro"); Out.Name = Request.Segments[1];
        for (UEdGraph* Graph : Blueprint->MacroGraphs)
            if (Graph != nullptr && Graph->GetName() == Out.Name) { Out.Graph = Graph; break; }
    }
    else return false;
    Out.Selector = Request.Selector;
    if (Out.Graph == nullptr || (Out.Kind == TEXT("event") && Out.EventRoot == nullptr))
    {
        OutError = {TEXT("not_found"), TEXT("The selected semantic child was not found")};
        return false;
    }
    return true;
}

bool IsSemanticPin(const UEdGraphNode* Node, const UEdGraphPin* Pin)
{
    using namespace UnrealMCP::BlueprintInspectionPrivate;
    if (!IsStructuralGraphPin(Node, Pin)) return false;
    return !(Pin->bHidden && Pin->LinkedTo.IsEmpty() && Pin->DefaultValue.IsEmpty()
        && Pin->DefaultObject == nullptr && Pin->DefaultTextValue.IsEmpty());
}

FString NodeKind(const UEdGraphNode* Node)
{
    if (Node->IsA<UK2Node_CallFunction>()) return TEXT("call_function");
    if (Node->IsA<UK2Node_VariableGet>()) return TEXT("get_variable");
    if (Node->IsA<UK2Node_VariableSet>()) return TEXT("set_variable");
    if (Node->IsA<UK2Node_CustomEvent>()) return TEXT("custom_event");
    if (Node->IsA<UK2Node_Event>()) return TEXT("event");
    if (Node->IsA<UK2Node_FunctionEntry>()) return TEXT("function_entry");
    if (Node->IsA<UK2Node_FunctionResult>()) return TEXT("function_result");
    if (Node->IsA<UK2Node_MacroInstance>()) return TEXT("call_macro");
    const FString ClassName = Node->GetClass()->GetName();
    if (ClassName.Contains(TEXT("IfThenElse"))) return TEXT("branch");
    if (ClassName.Contains(TEXT("DynamicCast"))) return TEXT("cast");
    if (ClassName.Contains(TEXT("Knot"))) return TEXT("reroute");
    if (ClassName.Contains(TEXT("MakeArray"))) return TEXT("make_array");
    if (ClassName.Contains(TEXT("MakeStruct"))) return TEXT("make_struct");
    if (ClassName.Contains(TEXT("BreakStruct"))) return TEXT("break_struct");
    if (ClassName.Contains(TEXT("Switch"))) return TEXT("switch");
    if (ClassName.Contains(TEXT("Select"))) return TEXT("select");
    return TEXT("node");
}

FString NodePrefix(const FString& Kind)
{
    if (Kind == TEXT("call_function") || Kind == TEXT("call_macro")) return TEXT("call");
    if (Kind == TEXT("get_variable")) return TEXT("get");
    if (Kind == TEXT("set_variable")) return TEXT("set");
    if (Kind == TEXT("branch")) return TEXT("branch");
    if (Kind == TEXT("event") || Kind == TEXT("custom_event")) return TEXT("event");
    if (Kind == TEXT("function_entry")) return TEXT("entry");
    if (Kind == TEXT("function_result")) return TEXT("result");
    if (Kind == TEXT("cast")) return TEXT("cast");
    return TEXT("node");
}

FString NodeName(UEdGraphNode* Node)
{
    if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
    {
        if (const UFunction* Function = Call->GetTargetFunction()) return Function->GetName();
    }
    if (const UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node)) return Variable->GetVarName().ToString();
    const FString Event = EventName(Node);
    if (!Event.IsEmpty()) return Event;
    return Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Left(256);
}

TArray<UEdGraphNode*> SortedNodes(const TSet<UEdGraphNode*>& Nodes)
{
    TArray<UEdGraphNode*> Result = Nodes.Array();
    Result.Sort([](const UEdGraphNode& Left, const UEdGraphNode& Right)
    {
        const FString A = GuidString(Left.NodeGuid) + TEXT("|") + Left.GetClass()->GetPathName();
        const FString B = GuidString(Right.NodeGuid) + TEXT("|") + Right.GetClass()->GetPathName();
        return A < B;
    });
    return Result;
}

TSet<UEdGraphNode*> EventSlice(UEdGraphNode* Root)
{
    TSet<UEdGraphNode*> Result;
    if (Root == nullptr) return Result;
    TArray<UEdGraphNode*> Queue{Root};
    int32 Offset = 0;
    while (Offset < Queue.Num() && Result.Num() < UnrealMCP::MaxGraphNodes)
    {
        UEdGraphNode* Node = Queue[Offset++];
        if (Node == nullptr || Result.Contains(Node)) continue;
        Result.Add(Node);
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsSemanticPin(Node, Pin)) continue;
            const bool bFollow = Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
            const bool bProducer = Pin->Direction == EGPD_Input;
            if (!bFollow && !bProducer) continue;
            TArray<UEdGraphPin*> Linked = Pin->LinkedTo;
            Linked.Sort([](const UEdGraphPin& Left, const UEdGraphPin& Right)
            {
                return GuidString(Left.GetOwningNodeUnchecked()->NodeGuid) < GuidString(Right.GetOwningNodeUnchecked()->NodeGuid);
            });
            for (UEdGraphPin* Other : Linked)
            {
                if (Other != nullptr && Other->GetOwningNodeUnchecked() != nullptr) Queue.Add(Other->GetOwningNodeUnchecked());
            }
        }
    }
    return Result;
}

TArray<UEdGraphNode*> TraversalOrder(const TSet<UEdGraphNode*>& Base, UEdGraphNode* PreferredRoot)
{
    TArray<UEdGraphNode*> Result;
    TSet<UEdGraphNode*> Seen;
    TArray<UEdGraphNode*> Queue;
    if (PreferredRoot != nullptr && Base.Contains(PreferredRoot)) Queue.Add(PreferredRoot);
    TArray<UEdGraphNode*> Sorted = SortedNodes(Base);
    for (UEdGraphNode* Node : Sorted)
    {
        if (Node->IsA<UK2Node_Event>() || Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_Tunnel>()) Queue.AddUnique(Node);
    }
    for (UEdGraphNode* Node : Sorted) Queue.AddUnique(Node);
    int32 Offset = 0;
    while (Offset < Queue.Num())
    {
        UEdGraphNode* Node = Queue[Offset++];
        if (Node == nullptr || Seen.Contains(Node) || !Base.Contains(Node)) continue;
        Seen.Add(Node);
        Result.Add(Node);
        TArray<UEdGraphNode*> Next;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsSemanticPin(Node, Pin) || Pin->Direction != EGPD_Output) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
                if (Linked != nullptr && Linked->GetOwningNodeUnchecked() != nullptr && Base.Contains(Linked->GetOwningNodeUnchecked()))
                    Next.AddUnique(Linked->GetOwningNodeUnchecked());
        }
        Next.Sort([](const UEdGraphNode& Left, const UEdGraphNode& Right)
            { return GuidString(Left.NodeGuid) < GuidString(Right.NodeGuid); });
        Queue.Insert(Next, Offset);
    }
    return Result;
}

int32 LinkCount(const TSet<UEdGraphNode*>& Nodes)
{
    int32 Count = 0;
    for (UEdGraphNode* Node : Nodes)
        for (UEdGraphPin* Pin : Node->Pins)
            if (IsSemanticPin(Node, Pin) && Pin->Direction == EGPD_Output)
                for (UEdGraphPin* Linked : Pin->LinkedTo)
                    if (Linked != nullptr && Nodes.Contains(Linked->GetOwningNodeUnchecked())) ++Count;
    return Count;
}

TSharedRef<FJsonObject> CallableRecord(const UFunction* Function)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), Function->GetName());
    Result->SetStringField(TEXT("owner_type"), Function->GetOuterUClass() != nullptr
        ? Function->GetOuterUClass()->GetPathName() : FString());
    Result->SetStringField(TEXT("dispatch"), Function->HasAnyFunctionFlags(FUNC_Static) ? TEXT("static") : TEXT("instance"));
    TArray<TSharedPtr<FJsonValue>> Traits;
    if (Function->HasAnyFunctionFlags(FUNC_BlueprintPure)) Traits.Add(MakeShared<FJsonValueString>(TEXT("pure")));
    if (Function->HasAnyFunctionFlags(FUNC_Const)) Traits.Add(MakeShared<FJsonValueString>(TEXT("const")));
    Result->SetArrayField(TEXT("traits"), Traits);
    TArray<TSharedPtr<FJsonValue>> Inputs;
    TArray<TSharedPtr<FJsonValue>> Outputs;
    for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
    {
        FProperty* Property = *It;
        FEdGraphPinType PinType;
        const TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
        Parameter->SetStringField(TEXT("name"), Property->GetName());
        if (GetDefault<UEdGraphSchema_K2>()->ConvertPropertyToPinType(Property, PinType))
            Parameter->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(PinType));
        const bool bOutput = Property->HasAnyPropertyFlags(CPF_ReturnParm | CPF_OutParm)
            && !Property->HasAnyPropertyFlags(CPF_ConstParm);
        (bOutput ? Outputs : Inputs).Add(MakeShared<FJsonValueObject>(Parameter));
    }
    Result->SetArrayField(TEXT("inputs"), Inputs);
    Result->SetArrayField(TEXT("outputs"), Outputs);
    return Result;
}

TSharedRef<FJsonObject> BuildGraphBody(const FGraphSelection& Selection, const TSet<UEdGraphNode*>& BaseNodes,
    const TSet<UEdGraphNode*>& Detailed, bool bVerbose, bool bPartial, int32 LimitBytes)
{
    TSet<UEdGraphNode*> Stubs;
    if (bPartial)
    {
        for (UEdGraphNode* Node : Detailed)
        {
            for (UEdGraphPin* Pin : Node->Pins)
            {
                for (UEdGraphPin* Linked : Pin->LinkedTo)
                {
                    UEdGraphNode* Other = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                    if (Other != nullptr && BaseNodes.Contains(Other) && !Detailed.Contains(Other)
                        && Stubs.Num() < MaximumBoundaryStubs) Stubs.Add(Other);
                }
            }
        }
    }
    TSet<UEdGraphNode*> Represented = Detailed;
    Represented.Append(Stubs);
    TArray<UEdGraphNode*> Ordered = SortedNodes(Represented);
    TMap<UEdGraphNode*, FString> NodeIds;
    TMap<FString, int32> Counters;
    for (UEdGraphNode* Node : Ordered)
    {
        const FString Prefix = NodePrefix(NodeKind(Node));
        NodeIds.Add(Node, Prefix + LexToString(++Counters.FindOrAdd(Prefix)));
    }

    TMap<FString, const UFunction*> CallableFunctions;
    for (UEdGraphNode* Node : Detailed)
    {
        if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
        {
            if (const UFunction* Function = Call->GetTargetFunction())
            {
                const FString Key = (Function->GetOuterUClass() != nullptr ? Function->GetOuterUClass()->GetPathName() : FString())
                    + TEXT("|") + Function->GetName();
                CallableFunctions.Add(Key, Function);
            }
        }
    }
    TArray<FString> CallableKeys;
    CallableFunctions.GetKeys(CallableKeys);
    CallableKeys.Sort();
    TMap<const UFunction*, FString> CallableIds;
    const TSharedRef<FJsonObject> Callables = MakeShared<FJsonObject>();
    int32 CallableIndex = 0;
    for (const FString& Key : CallableKeys)
    {
        const UFunction* Function = CallableFunctions[Key];
        const FString Id = TEXT("fn") + LexToString(++CallableIndex);
        CallableIds.Add(Function, Id);
        Callables->SetObjectField(Id, CallableRecord(Function));
    }

    TArray<TSharedPtr<FJsonValue>> NodeValues;
    int32 ReturnedLinks = 0;
    for (UEdGraphNode* Node : Ordered)
    {
        const bool bStub = Stubs.Contains(Node);
        const FString Kind = NodeKind(Node);
        const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
        Value->SetStringField(TEXT("id"), NodeIds[Node]);
        Value->SetStringField(TEXT("kind"), Kind);
        const FString Name = NodeName(Node);
        if (!Name.IsEmpty()) Value->SetStringField(TEXT("name"), Name);
        if (bStub) Value->SetBoolField(TEXT("omitted_body"), true);
        if (!bStub)
        {
            if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
            {
                if (const UFunction* Function = Call->GetTargetFunction())
                {
                    if (const FString* Callable = CallableIds.Find(Function)) Value->SetStringField(TEXT("callable"), *Callable);
                    if (!Call->IsNodePure()) Value->SetStringField(TEXT("target"), TEXT("self"));
                }
            }
            if (const UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
                Value->SetStringField(TEXT("variable"), Variable->GetVarName().ToString());
            TArray<TSharedPtr<FJsonValue>> Inputs;
            TArray<TSharedPtr<FJsonValue>> Outputs;
            const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (!IsSemanticPin(Node, Pin)) continue;
                const TSharedRef<FJsonObject> PinValue = MakeShared<FJsonObject>();
                PinValue->SetStringField(TEXT("name"), Pin->PinName.ToString());
                PinValue->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(Pin->PinType));
                if (Pin->Direction == EGPD_Input && Pin->LinkedTo.IsEmpty())
                {
                    const FString DefaultText = Pin->DefaultObject != nullptr ? Pin->DefaultObject->GetPathName()
                        : Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text ? Pin->DefaultTextValue.ToString() : Pin->DefaultValue;
                    const TSharedRef<FJsonObject> Default = UnrealMCP::K2TypeCodec::EncodeDefault(Pin->PinType, DefaultText);
                    PinValue->SetObjectField(TEXT("default"), Default);
                    FString DefaultKind;
                    if (Kind == TEXT("call_function") && Default->TryGetStringField(TEXT("kind"), DefaultKind)
                        && DefaultKind != TEXT("engine_default")) Arguments->SetObjectField(Pin->PinName.ToString(), Default);
                }
                (Pin->Direction == EGPD_Input ? Inputs : Outputs).Add(MakeShared<FJsonValueObject>(PinValue));
            }
            Value->SetArrayField(TEXT("inputs"), Inputs);
            Value->SetArrayField(TEXT("outputs"), Outputs);
            if (!Arguments->Values.IsEmpty()) Value->SetObjectField(TEXT("arguments"), Arguments);
            if (bVerbose)
            {
                const TSharedRef<FJsonObject> Debug = MakeShared<FJsonObject>();
                Debug->SetStringField(TEXT("node_guid"), GuidString(Node->NodeGuid));
                Debug->SetNumberField(TEXT("x"), Node->NodePosX);
                Debug->SetNumberField(TEXT("y"), Node->NodePosY);
                const TSharedRef<FJsonObject> Pins = MakeShared<FJsonObject>();
                for (UEdGraphPin* Pin : Node->Pins)
                    if (IsSemanticPin(Node, Pin)) Pins->SetStringField(Pin->PinName.ToString(), GuidString(Pin->PinId));
                Debug->SetObjectField(TEXT("pins"), Pins);
                Value->SetObjectField(TEXT("debug"), Debug);
            }
        }
        const TSharedRef<FJsonObject> Links = MakeShared<FJsonObject>();
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!IsSemanticPin(Node, Pin) || Pin->Direction != EGPD_Output) continue;
            TArray<TSharedPtr<FJsonValue>> Destinations;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
            {
                UEdGraphNode* Other = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                if (Other == nullptr || !Represented.Contains(Other)) continue;
                const TSharedRef<FJsonObject> Destination = MakeShared<FJsonObject>();
                Destination->SetStringField(TEXT("to"), NodeIds[Other] + TEXT(".") + Linked->PinName.ToString());
                if (bVerbose)
                {
                    const TSharedRef<FJsonObject> Debug = MakeShared<FJsonObject>();
                    Debug->SetStringField(TEXT("from_node_guid"), GuidString(Node->NodeGuid));
                    Debug->SetStringField(TEXT("from_pin_guid"), GuidString(Pin->PinId));
                    Debug->SetStringField(TEXT("to_node_guid"), GuidString(Other->NodeGuid));
                    Debug->SetStringField(TEXT("to_pin_guid"), GuidString(Linked->PinId));
                    Destination->SetObjectField(TEXT("debug"), Debug);
                }
                Destinations.Add(MakeShared<FJsonValueObject>(Destination));
                ++ReturnedLinks;
            }
            Destinations.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
                { return Left->AsObject()->GetStringField(TEXT("to")) < Right->AsObject()->GetStringField(TEXT("to")); });
            if (!Destinations.IsEmpty()) Links->SetArrayField(Pin->PinName.ToString(), Destinations);
        }
        if (!Links->Values.IsEmpty()) Value->SetObjectField(TEXT("links"), Links);
        NodeValues.Add(MakeShared<FJsonValueObject>(Value));
    }
    const TSharedRef<FJsonObject> Graph = MakeShared<FJsonObject>();
    Graph->SetStringField(TEXT("kind"), Selection.Kind);
    Graph->SetStringField(TEXT("name"), Selection.Name);
    if (bVerbose)
    {
        const TSharedRef<FJsonObject> Debug = MakeShared<FJsonObject>();
        Debug->SetStringField(TEXT("graph_guid"), GuidString(Selection.Graph->GraphGuid));
        Graph->SetObjectField(TEXT("debug"), Debug);
    }
    Graph->SetObjectField(TEXT("callables"), Callables);
    Graph->SetArrayField(TEXT("nodes"), NodeValues);
    const TSharedRef<FJsonObject> Status = MakeShared<FJsonObject>();
    Status->SetBoolField(TEXT("complete"), !bPartial);
    Status->SetNumberField(TEXT("total_nodes"), BaseNodes.Num());
    Status->SetNumberField(TEXT("total_links"), LinkCount(BaseNodes));
    Status->SetNumberField(TEXT("detailed_nodes"), Detailed.Num());
    Status->SetNumberField(TEXT("boundary_stub_nodes"), Stubs.Num());
    Status->SetNumberField(TEXT("returned_links"), ReturnedLinks);
    if (bPartial)
    {
        Status->SetStringField(TEXT("reason"), TEXT("graph_limit_exceeded"));
        Status->SetStringField(TEXT("selection_strategy"), TEXT("execution_from_entry"));
        Status->SetNumberField(TEXT("omitted_nodes"), FMath::Max(0, BaseNodes.Num() - Detailed.Num() - Stubs.Num()));
        Status->SetNumberField(TEXT("omitted_links"), FMath::Max(0, LinkCount(BaseNodes) - ReturnedLinks));
        Status->SetNumberField(TEXT("limit_bytes"), LimitBytes);
    }
    Graph->SetObjectField(TEXT("graph_status"), Status);
    return Graph;
}

int32 SerializedBytes(const TSharedRef<FJsonObject>& Object)
{
    FString Text;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
    if (!FJsonSerializer::Serialize(Object, Writer)) return MAX_int32;
    FTCHARToUTF8 Encoded(*Text);
    return Encoded.Length();
}

TSharedRef<FJsonObject> BuildGraphResult(const FString& AssetPath, const FString& Snapshot,
    const FClassification& Classification, const FGraphSelection& Selection,
    const TSet<UEdGraphNode*>& BaseNodes, const TSet<UEdGraphNode*>& Detailed,
    bool bVerbose, bool bPartial)
{
    const TSharedRef<FJsonObject> Result = BaseResult(AssetPath, Snapshot, Classification);
    const TSharedRef<FJsonObject> Selected = MakeShared<FJsonObject>();
    Selected->SetStringField(TEXT("selector"), Selection.Selector);
    Result->SetObjectField(TEXT("selection"), Selected);
    if (Selection.Kind == TEXT("function"))
    {
        const TSharedRef<FJsonObject> Header = MakeShared<FJsonObject>();
        Header->SetStringField(TEXT("name"), Selection.Name);
        Header->SetObjectField(TEXT("signature"), SignatureFromGraph(Selection.Graph));
        TArray<TSharedPtr<FJsonValue>> Locals;
        if (UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(Selection.Graph)))
        {
            for (const FBPVariableDescription& Local : Entry->LocalVariables)
            {
                const TSharedRef<FJsonObject> Value = MakeShared<FJsonObject>();
                Value->SetStringField(TEXT("name"), Local.VarName.ToString());
                Value->SetObjectField(TEXT("type"), UnrealMCP::K2TypeCodec::EncodeType(Local.VarType));
                Value->SetObjectField(TEXT("value"), UnrealMCP::K2TypeCodec::EncodeDefault(Local.VarType, Local.DefaultValue));
                Locals.Add(MakeShared<FJsonValueObject>(Value));
            }
        }
        Header->SetArrayField(TEXT("local_variables"), Locals);
        Result->SetObjectField(TEXT("function"), Header);
    }
    else if (Selection.Kind == TEXT("macro"))
    {
        const TSharedRef<FJsonObject> Header = MakeShared<FJsonObject>();
        Header->SetStringField(TEXT("name"), Selection.Name);
        Header->SetObjectField(TEXT("signature"), MacroSignature(Selection.Graph));
        Result->SetObjectField(TEXT("macro"), Header);
    }
    Result->SetObjectField(TEXT("graph"), BuildGraphBody(
        Selection, BaseNodes, Detailed, bVerbose, bPartial, CompleteGraphBytes));
    return Result;
}

bool BuildSelectedGraph(const FRequest& Request, const FString& AssetPath, const FString& Snapshot,
    const FClassification& Classification, const FGraphSelection& Selection,
    TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError)
{
    if (Request.bHasPaging)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Graph selectors do not accept paging parameters")};
        return false;
    }
    if (Selection.Kind == TEXT("interface_function"))
    {
        if (Request.bHasPartialFlag)
        {
            OutError = {TEXT("invalid_argument"), TEXT("allow_partial_graph applies only to graph selectors")};
            return false;
        }
        const TSharedRef<FJsonObject> Result = BaseResult(AssetPath, Snapshot, Classification);
        const TSharedRef<FJsonObject> Selected = MakeShared<FJsonObject>();
        Selected->SetStringField(TEXT("selector"), Selection.Selector);
        Result->SetObjectField(TEXT("selection"), Selected);
        const TSharedRef<FJsonObject> Function = MakeShared<FJsonObject>();
        Function->SetStringField(TEXT("name"), Selection.Name);
        Function->SetStringField(TEXT("owner_type"), Classification.RepresentedClass);
        Function->SetStringField(TEXT("dispatch"), TEXT("interface_message"));
        Function->SetObjectField(TEXT("signature"), SignatureFromGraph(Selection.Graph));
        Result->SetObjectField(TEXT("interface_function"), Function);
        OutResult = Result;
        return true;
    }
    TSet<UEdGraphNode*> BaseNodes;
    if (Selection.EventRoot != nullptr) BaseNodes = EventSlice(Selection.EventRoot);
    else for (UEdGraphNode* Node : Selection.Graph->Nodes) if (Node != nullptr) BaseNodes.Add(Node);
    if (BaseNodes.Num() > UnrealMCP::MaxGraphNodes)
    {
        OutError = {TEXT("data_limit_exceeded"), TEXT("The selected graph exceeds the structural node limit")};
        return false;
    }
    TSharedRef<FJsonObject> Complete = BuildGraphResult(
        AssetPath, Snapshot, Classification, Selection, BaseNodes, BaseNodes, Request.bVerbose, false);
    if (SerializedBytes(Complete) <= CompleteGraphBytes)
    {
        OutResult = Complete;
        return true;
    }
    if (!Request.bAllowPartialGraph)
    {
        OutError = {TEXT("data_limit_exceeded"), TEXT("The selected graph exceeds the complete graph response limit")};
        return false;
    }
    const TArray<UEdGraphNode*> Order = TraversalOrder(BaseNodes, Selection.EventRoot);
    int32 Low = 1;
    int32 High = Order.Num();
    int32 Best = 0;
    TSharedPtr<FJsonObject> BestResult;
    while (Low <= High)
    {
        const int32 Middle = Low + (High - Low) / 2;
        TSet<UEdGraphNode*> Detailed;
        for (int32 Index = 0; Index < Middle; ++Index) Detailed.Add(Order[Index]);
        TSharedRef<FJsonObject> Candidate = BuildGraphResult(
            AssetPath, Snapshot, Classification, Selection, BaseNodes, Detailed, Request.bVerbose, true);
        if (SerializedBytes(Candidate) <= CompleteGraphBytes)
        {
            Best = Middle;
            BestResult = Candidate;
            Low = Middle + 1;
        }
        else High = Middle - 1;
    }
    if (Best == 0 || !BestResult.IsValid())
    {
        OutError = {TEXT("data_limit_exceeded"), TEXT("No coherent partial graph slice fits the response limit")};
        return false;
    }
    OutResult = BestResult;
    return true;
}

TSharedPtr<FJsonValue> ExportElement(FProperty* Property, const void* Address, UObject* Owner)
{
    if (Property == nullptr || Address == nullptr) return MakeShared<FJsonValueNull>();
    if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
        return MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Address));
    if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
        return MakeShared<FJsonValueNumber>(Numeric->IsFloatingPoint()
            ? Numeric->GetFloatingPointPropertyValue(Address)
            : static_cast<double>(Numeric->GetSignedIntPropertyValue(Address)));
    if (const FNameProperty* Name = CastField<FNameProperty>(Property))
        return MakeShared<FJsonValueString>(Name->GetPropertyValue(Address).ToString());
    if (const FStrProperty* String = CastField<FStrProperty>(Property))
        return MakeShared<FJsonValueString>(String->GetPropertyValue(Address));
    if (const FTextProperty* Text = CastField<FTextProperty>(Property))
        return MakeShared<FJsonValueString>(Text->GetPropertyValue(Address).ToString());
    if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        const UObject* Value = ObjectProperty->GetObjectPropertyValue(Address);
        return MakeShared<FJsonValueString>(Value != nullptr ? Value->GetPathName() : FString());
    }
    FString Exported;
    Property->ExportText_Direct(Exported, Address, nullptr, Owner, PPF_None);
    return MakeShared<FJsonValueString>(Exported.Left(4096));
}

bool PageReflectedCollection(UObject* Object, const TCHAR* PropertyName, const FRequest& Request,
    TArray<TSharedPtr<FJsonValue>>& OutItems, FString& OutKind, int32& OutCount)
{
    if (Object == nullptr) return false;
    FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName);
    void* Address = Property != nullptr ? Property->ContainerPtrToValuePtr<void>(Object) : nullptr;
    const int64 Start64 = static_cast<int64>(Request.PageIndex) * Request.PageSize;
    if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Helper(Array, Address);
        OutKind = TEXT("array"); OutCount = Helper.Num();
        const int32 Start = static_cast<int32>(FMath::Min<int64>(Start64, OutCount));
        const int32 End = FMath::Min(Start + Request.PageSize, OutCount);
        for (int32 Index = Start; Index < End; ++Index) OutItems.Add(ExportElement(Array->Inner, Helper.GetRawPtr(Index), Object));
        return true;
    }
    if (const FSetProperty* Set = CastField<FSetProperty>(Property))
    {
        FScriptSetHelper Helper(Set, Address);
        OutKind = TEXT("set"); OutCount = Helper.Num();
        TArray<TSharedPtr<FJsonValue>> All;
        for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
            if (Helper.IsValidIndex(Index)) All.Add(ExportElement(Set->ElementProp, Helper.GetElementPtr(Index), Object));
        All.Sort([](const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
            { FString A, B; Left->TryGetString(A); Right->TryGetString(B); return A < B; });
        const int32 Start = static_cast<int32>(FMath::Min<int64>(Start64, All.Num()));
        const int32 End = FMath::Min(Start + Request.PageSize, All.Num());
        for (int32 Index = Start; Index < End; ++Index) OutItems.Add(All[Index]);
        return true;
    }
    if (const FMapProperty* Map = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper Helper(Map, Address);
        OutKind = TEXT("map"); OutCount = Helper.Num();
        TArray<TSharedPtr<FJsonValue>> All;
        for (int32 Index = 0; Index < Helper.GetMaxIndex(); ++Index)
        {
            if (!Helper.IsValidIndex(Index)) continue;
            const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetField(TEXT("key"), ExportElement(Map->KeyProp, Helper.GetKeyPtr(Index), Object));
            Entry->SetField(TEXT("value"), ExportElement(Map->ValueProp, Helper.GetValuePtr(Index), Object));
            All.Add(MakeShared<FJsonValueObject>(Entry));
        }
        const int32 Start = static_cast<int32>(FMath::Min<int64>(Start64, All.Num()));
        const int32 End = FMath::Min(Start + Request.PageSize, All.Num());
        for (int32 Index = Start; Index < End; ++Index) OutItems.Add(All[Index]);
        return true;
    }
    return false;
}

TSharedRef<FJsonObject> PageResult(const FString& AssetPath, const FString& Snapshot,
    const FClassification& Classification, const FRequest& Request, const FString& Kind,
    int32 Count, const TArray<TSharedPtr<FJsonValue>>& Items)
{
    const TSharedRef<FJsonObject> Result = BaseResult(AssetPath, Snapshot, Classification);
    const TSharedRef<FJsonObject> Selection = MakeShared<FJsonObject>();
    Selection->SetStringField(TEXT("selector"), Request.Selector);
    Result->SetObjectField(TEXT("selection"), Selection);
    const TSharedRef<FJsonObject> Page = MakeShared<FJsonObject>();
    Page->SetStringField(TEXT("kind"), Kind);
    Page->SetNumberField(TEXT("count"), Count);
    Page->SetNumberField(TEXT("page_size"), Request.PageSize);
    Page->SetNumberField(TEXT("page_index"), Request.PageIndex);
    Page->SetArrayField(TEXT("items"), Items);
    Page->SetBoolField(TEXT("has_more"), static_cast<int64>(Request.PageIndex + 1) * Request.PageSize < Count);
    Result->SetObjectField(TEXT("collection"), Page);
    return Result;
}

bool BuildCollectionSelection(UBlueprint* Blueprint, const FClassification& Classification, const FRequest& Request,
    const FString& AssetPath, const FString& Snapshot, TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError)
{
    if (Request.bHasPartialFlag)
    {
        OutError = {TEXT("invalid_argument"), TEXT("allow_partial_graph applies only to graph selectors")};
        return false;
    }
    UObject* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
        ? Blueprint->GeneratedClass->GetDefaultObject(false) : nullptr;
    TArray<TSharedPtr<FJsonValue>> Items;
    FString Kind;
    int32 Count = 0;
    if (Request.Segments == TArray<FString>{TEXT("properties"), TEXT("actor"), TEXT("tags")})
    {
        if (!PageReflectedCollection(Defaults, TEXT("Tags"), Request, Items, Kind, Count)) goto NotFound;
    }
    else if (Request.Segments == TArray<FString>{TEXT("properties"), TEXT("component"), TEXT("tags")})
    {
        if (!PageReflectedCollection(Defaults, TEXT("ComponentTags"), Request, Items, Kind, Count)) goto NotFound;
    }
    else if (Request.Segments == TArray<FString>{TEXT("properties"), TEXT("player_controller"), TEXT("click_keys")})
    {
        if (!PageReflectedCollection(Defaults, TEXT("ClickEventKeys"), Request, Items, Kind, Count)) goto NotFound;
    }
    else if (Request.Segments == TArray<FString>{TEXT("world_partition_streaming"), TEXT("shapes")})
    {
        if (!PageReflectedCollection(Defaults, TEXT("StreamingSourceShapes"), Request, Items, Kind, Count)) goto NotFound;
    }
    else if (Request.Segments == TArray<FString>{TEXT("properties"), TEXT("implemented_interfaces")})
    {
        Kind = TEXT("array"); Count = Blueprint->ImplementedInterfaces.Num();
        const int64 Start = static_cast<int64>(Request.PageIndex) * Request.PageSize;
        const int32 End = FMath::Min<int64>(Start + Request.PageSize, Count);
        for (int32 Index = static_cast<int32>(FMath::Min<int64>(Start, Count)); Index < End; ++Index)
        {
            const UClass* Interface = Blueprint->ImplementedInterfaces[Index].Interface;
            Items.Add(MakeShared<FJsonValueString>(Interface != nullptr ? Interface->GetPathName() : FString()));
        }
    }
    else if (Request.Segments.Num() == 3 && Request.Segments[0] == TEXT("properties")
        && Request.Segments[1] == TEXT("variables"))
    {
        const FBPVariableDescription* Variable = Blueprint->NewVariables.FindByPredicate(
            [&Request](const FBPVariableDescription& Candidate) { return Candidate.VarName.ToString() == Request.Segments[2]; });
        if (Variable == nullptr) goto NotFound;
        const TSharedRef<FJsonObject> Default = UnrealMCP::K2TypeCodec::EncodeDefault(Variable->VarType, Variable->DefaultValue);
        if (!Default->TryGetStringField(TEXT("kind"), Kind) || (Kind != TEXT("array") && Kind != TEXT("set") && Kind != TEXT("map"))) goto NotFound;
        const TArray<TSharedPtr<FJsonValue>>* All = nullptr;
        if (!Default->TryGetArrayField(Kind == TEXT("map") ? TEXT("entries") : TEXT("items"), All) || All == nullptr) goto NotFound;
        Count = All->Num();
        const int64 Start = static_cast<int64>(Request.PageIndex) * Request.PageSize;
        const int32 End = FMath::Min<int64>(Start + Request.PageSize, Count);
        for (int32 Index = static_cast<int32>(FMath::Min<int64>(Start, Count)); Index < End; ++Index) Items.Add((*All)[Index]);
    }
    else return false;
    OutResult = PageResult(AssetPath, Snapshot, Classification, Request, Kind, Count, Items);
    return true;

NotFound:
    OutError = {TEXT("not_found"), TEXT("The selected collection is unavailable for this asset")};
    return false;
}

bool BuildComponentSelection(UBlueprint* Blueprint, const FClassification& Classification, const FRequest& Request,
    const FString& AssetPath, const FString& Snapshot, TSharedPtr<FJsonObject>& OutResult, FUnrealMCPError& OutError)
{
    if (Request.bHasPartialFlag)
    {
        OutError = {TEXT("invalid_argument"), TEXT("allow_partial_graph applies only to graph selectors")};
        return false;
    }
    if (Blueprint->SimpleConstructionScript == nullptr) return false;
    TArray<USCS_Node*> Nodes = Blueprint->SimpleConstructionScript->GetAllNodes();
    Nodes.Sort([](const USCS_Node& Left, const USCS_Node& Right)
        { return Left.GetVariableName().LexicalLess(Right.GetVariableName()); });
    if (Request.Segments.Num() == 1)
    {
        TArray<TSharedPtr<FJsonValue>> Items;
        const int64 Start = static_cast<int64>(Request.PageIndex) * Request.PageSize;
        const int32 End = FMath::Min<int64>(Start + Request.PageSize, Nodes.Num());
        for (int32 Index = static_cast<int32>(FMath::Min<int64>(Start, Nodes.Num())); Index < End; ++Index)
        {
            USCS_Node* Node = Nodes[Index];
            const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
            Item->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
            Item->SetStringField(TEXT("class"), Node->ComponentClass != nullptr ? Node->ComponentClass->GetPathName() : FString());
            Item->SetBoolField(TEXT("scene_component"), Node->ComponentClass != nullptr && Node->ComponentClass->IsChildOf(USceneComponent::StaticClass()));
            Item->SetStringField(TEXT("selector"), TEXT("components/") + EncodeSegment(Node->GetVariableName().ToString()));
            Items.Add(MakeShared<FJsonValueObject>(Item));
        }
        OutResult = PageResult(AssetPath, Snapshot, Classification, Request, TEXT("array"), Nodes.Num(), Items);
        return true;
    }
    if (Request.bHasPaging)
    {
        OutError = {TEXT("invalid_argument"), TEXT("Paging parameters apply to the components collection, not one component")};
        return false;
    }
    USCS_Node** Found = Nodes.FindByPredicate([&Request](USCS_Node* Node)
        { return Node != nullptr && Node->GetVariableName().ToString() == Request.Segments[1]; });
    if (Found == nullptr)
    {
        OutError = {TEXT("not_found"), TEXT("The selected component was not found")};
        return false;
    }
    USCS_Node* Node = *Found;
    const TSharedRef<FJsonObject> Result = BaseResult(AssetPath, Snapshot, Classification);
    const TSharedRef<FJsonObject> Selection = MakeShared<FJsonObject>();
    Selection->SetStringField(TEXT("selector"), Request.Selector);
    Result->SetObjectField(TEXT("selection"), Selection);
    const TSharedRef<FJsonObject> Component = MakeShared<FJsonObject>();
    Component->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
    Component->SetStringField(TEXT("class"), Node->ComponentClass != nullptr ? Node->ComponentClass->GetPathName() : FString());
    Component->SetBoolField(TEXT("scene_component"), Node->ComponentClass != nullptr && Node->ComponentClass->IsChildOf(USceneComponent::StaticClass()));
    Component->SetBoolField(TEXT("root"), Blueprint->SimpleConstructionScript->GetRootNodes().Contains(Node));
    if (Node->ComponentTemplate != nullptr)
    {
        TArray<TSharedPtr<FJsonValue>> Defaults;
        for (TFieldIterator<FProperty> It(Node->ComponentTemplate->GetClass(), EFieldIterationFlags::IncludeSuper); It; ++It)
        {
            FProperty* Property = *It;
            if (Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated)
                && !UnrealMCP::PropertyCodec::IsIdenticalToArchetype(Node->ComponentTemplate, Property)
                && Defaults.Num() < 32)
            {
                const TSharedRef<FJsonObject> Default = MakeShared<FJsonObject>();
                Default->SetStringField(TEXT("name"), Property->GetName());
                FString Exported;
                UnrealMCP::PropertyCodec::ExportValueText(Node->ComponentTemplate, Property, Exported);
                Default->SetStringField(TEXT("value"), Exported.Left(4096));
                Defaults.Add(MakeShared<FJsonValueObject>(Default));
            }
        }
        Component->SetArrayField(TEXT("changed_defaults"), Defaults);
    }
    Result->SetObjectField(TEXT("component"), Component);
    OutResult = Result;
    return true;
}
}

bool FUnrealMCPAssetInspectionService::Execute(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    using namespace UnrealMCP::AssetInspectionPrivate;
    check(IsInGameThread());
    FRequest Request;
    if (!DecodeRequest(Arguments, Request, OutError)) return false;
    IAssetRegistry& Registry = FAssetRegistryModule::GetRegistry();
    const FAssetData AssetData = Registry.GetAssetByObjectPath(FSoftObjectPath(Request.AssetPath));
    if (!AssetData.IsValid())
    {
        OutError = {TEXT("not_found"), TEXT("The requested /Game asset was not found")};
        return false;
    }
    UObject* AssetObject = AssetData.GetAsset();
    if (AssetObject == nullptr)
    {
        OutError = {TEXT("busy"), TEXT("The requested asset could not be loaded"), MakeShared<FJsonObject>(), true};
        return false;
    }
    UBlueprint* Blueprint = Cast<UBlueprint>(AssetObject);
    const FClassification Classification = Classify(AssetObject, Blueprint);
    const UPackage* Package = AssetObject->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();
    const TEnumAsByte<EBlueprintStatus> StatusBefore = Blueprint != nullptr
        ? Blueprint->Status : TEnumAsByte<EBlueprintStatus>(BS_Unknown);
    const FString Snapshot = BuildStableSnapshot(AssetObject, Blueprint);

    if (Request.Segments.IsEmpty())
    {
        if (Request.bHasPaging)
        {
            OutError = {TEXT("invalid_argument"), TEXT("Paging parameters require a pageable collection selector")};
            return false;
        }
        if (Request.bHasPartialFlag)
        {
            OutError = {TEXT("invalid_argument"), TEXT("allow_partial_graph applies only to graph selectors")};
            return false;
        }
        const TSharedRef<FJsonObject> Result = BaseResult(Request.AssetPath, Snapshot, Classification);
        if (!Classification.bDeepBlueprint)
        {
            TArray<TSharedPtr<FJsonValue>> Limitations;
            Limitations.Add(MakeShared<FJsonValueString>(Classification.bMedia ? TEXT("media_type_only") : TEXT("unsupported_family")));
            Limitations.Add(MakeShared<FJsonValueString>(TEXT("no_media_or_bulk_data")));
            Result->SetArrayField(TEXT("limitations"), Limitations);
        }
        else
        {
            TArray<TSharedPtr<FJsonValue>> Selectors;
            AddFamilySemantics(Blueprint, Classification, Result, Selectors);
            AddRootDeclarations(Blueprint, Classification, Result, Selectors);
            if (!Selectors.IsEmpty()) Result->SetArrayField(TEXT("selectors"), Selectors);
        }
        OutResult = Result;
    }
    else
    {
        if (!Classification.bDeepBlueprint)
        {
            OutError = {TEXT("unsupported_type"), TEXT("This asset family supports identity inspection only")};
            return false;
        }
        FGraphSelection GraphSelection;
        FUnrealMCPError SelectionError;
        if (FindGraphSelection(Blueprint, Request, Classification, GraphSelection, SelectionError))
        {
            if (!BuildSelectedGraph(Request, Request.AssetPath, Snapshot, Classification, GraphSelection, OutResult, OutError)) return false;
        }
        else if (!SelectionError.Code.IsEmpty())
        {
            OutError = SelectionError;
            return false;
        }
        else if (Request.Segments[0] == TEXT("components") && (Request.Segments.Num() == 1 || Request.Segments.Num() == 2))
        {
            if (!BuildComponentSelection(Blueprint, Classification, Request, Request.AssetPath, Snapshot, OutResult, OutError))
            {
                if (OutError.Code.IsEmpty()) OutError = {TEXT("not_found"), TEXT("The selected component collection is unavailable")};
                return false;
            }
        }
        else
        {
            if (!BuildCollectionSelection(Blueprint, Classification, Request, Request.AssetPath, Snapshot, OutResult, OutError))
            {
                if (OutError.Code.IsEmpty()) OutError = {TEXT("not_found"), TEXT("The selector is not available for this asset")};
                return false;
            }
        }
    }
    if ((Package != nullptr && Package->IsDirty() != bDirtyBefore)
        || (Blueprint != nullptr && Blueprint->Status != StatusBefore))
    {
        OutError = {TEXT("internal_error"), TEXT("Asset inspection unexpectedly changed editor state")};
        OutResult.Reset();
        return false;
    }
    if (BuildStableSnapshot(AssetObject, Blueprint) != Snapshot)
    {
        OutError = {TEXT("stale_precondition"), TEXT("The asset changed during inspection"), MakeShared<FJsonObject>(), true};
        OutResult.Reset();
        return false;
    }
    return true;
}
