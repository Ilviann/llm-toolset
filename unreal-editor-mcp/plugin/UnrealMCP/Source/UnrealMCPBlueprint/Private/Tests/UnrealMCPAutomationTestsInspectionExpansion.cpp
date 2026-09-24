#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPBlueprintAutomationTestSupport.h"
#include "UnrealMCPAssetInspectionAdapters.h"
#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPAssetInspectionService.h"
#include "UnrealMCPBlueprintMutator.h"
#include "UnrealMCPGameplayAttributeInspection.h"
#include "UnrealMCPK2TypeCodec.h"
#include "UnrealMCPPropertyCodec.h"
#include "UnrealMCPGameDataValueCodec.h"
#include "UObject/StructOnScope.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Kismet/BlueprintFunctionLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPInspectionExpansionTest,
    "UnrealMCP.AssetInspect.LibrariesInheritedComponentsAndGraphScope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPInspectionExpansionTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString Root = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FUnrealMCPBlueprintInspector Inspector;
    const auto Registry = MakeShared<FUnrealMCPAssetFamilyRegistry>();
    FUnrealMCPError RegistryError;
    if (!UnrealMCP::AssetInspection::RegisterBuiltInAdapters(*Registry, RegistryError) || !Registry->Freeze(RegistryError)) return false;
    FUnrealMCPAssetInspectionService Service(Registry);
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    for (const EBlueprintType Type : {BPTYPE_FunctionLibrary, BPTYPE_MacroLibrary})
    {
        const FString Name = Type == BPTYPE_FunctionLibrary ? TEXT("Functions") : TEXT("Macros");
        UBlueprint* Library = FKismetEditorUtilities::CreateBlueprint(Type == BPTYPE_FunctionLibrary
            ? UBlueprintFunctionLibrary::StaticClass() : AActor::StaticClass(), CreatePackage(*(Root + TEXT("/") + Name)),
            FName(*Name), Type, TEXT("UnrealMCP.Tests"));
        if (!TestNotNull(TEXT("library fixture"), Library)) return false;
        FAssetRegistryModule::AssetCreated(Library);
        UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Library, TEXT("Compute"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        if (Type == BPTYPE_FunctionLibrary) FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Library, Graph, true, nullptr);
        else FBlueprintEditorUtils::AddMacroGraph(Library, Graph, true, nullptr);
        const auto Query = InspectArguments(Library->GetPathName(), 1);
        if (!TestTrue(TEXT("library summary"), Inspector.Execute(Query, Result, Error))) return false;
        TestEqual(TEXT("library exact family"), Result->GetStringField(TEXT("blueprint_family")),
            Type == BPTYPE_FunctionLibrary ? FString(TEXT("function_library")) : FString(TEXT("macro_library")));
        TestTrue(TEXT("library inspection only"), Result->GetObjectField(TEXT("family_capabilities"))->GetBoolField(TEXT("inspection_only")));
        const FString Snapshot = Result->GetStringField(TEXT("snapshot_id"));
        Query->SetStringField(TEXT("graph_name"), TEXT("Compute"));
        if (!TestTrue(TEXT("name selects graph contents by default"), Inspector.Execute(Query, Result, Error))) return false;
        TestEqual(TEXT("summary and graph snapshots agree"), Result->GetStringField(TEXT("snapshot_id")), Snapshot);
        TestTrue(TEXT("selected graph pages"), Result->GetBoolField(TEXT("has_more")));
        const FString Cursor = Result->GetStringField(TEXT("next_cursor"));
        Query->SetStringField(TEXT("graph_id"), Graph->GraphGuid.ToString(EGuidFormats::Digits));
        TestFalse(TEXT("two graph selectors reject"), Inspector.Execute(Query, Result, Error));
        Query->RemoveField(TEXT("graph_id")); Query->RemoveField(TEXT("graph_name"));
        Query->SetArrayField(TEXT("sections"), {MakeShared<FUnrealMCPValueString>(TEXT("nodes"))});
        TestFalse(TEXT("unscoped graph contents reject"), Inspector.Execute(Query, Result, Error));
        Query->RemoveField(TEXT("sections")); Query->SetStringField(TEXT("graph_name"), TEXT("Missing"));
        TestFalse(TEXT("missing graph name rejects"), Inspector.Execute(Query, Result, Error));
        Graph->Nodes[0]->NodePosX += 10;
        const auto Continue = MakeShared<FUnrealMCPRecord>(); Continue->SetStringField(TEXT("cursor"), Cursor);
        TestFalse(TEXT("changed library cursor rejects"), Inspector.Execute(Continue, Result, Error));
        TestEqual(TEXT("changed library cursor stale"), Error.Code, FString(TEXT("stale_precondition")));
        const auto Public = MakeShared<FUnrealMCPRecord>(); Public->SetStringField(TEXT("asset_path"), Library->GetPathName());
        if (!TestTrue(TEXT("public library root"), Service.Execute(Public, Result, Error))) return false;
        TestEqual(TEXT("Actor scoped macro library is not Actor"), Result->GetObjectField(TEXT("asset"))->GetStringField(TEXT("type")),
            Type == BPTYPE_FunctionLibrary ? FString(TEXT("function_library_blueprint")) : FString(TEXT("macro_library_blueprint")));
        const auto Discover = MakeShared<FUnrealMCPRecord>(); Discover->SetStringField(TEXT("mode"), TEXT("discover"));
        Discover->SetStringField(TEXT("package_path"), Root); Discover->SetStringField(TEXT("asset_name"), Name);
        if (!TestTrue(TEXT("library discovery"), Inspector.Execute(Discover, Result, Error))) return false;
        TestEqual(TEXT("discovery includes library"), Result->GetArrayField(TEXT("records")).Num(), 1);
        const auto Mutate = MakeShared<FUnrealMCPRecord>(); Mutate->SetStringField(TEXT("asset_path"), Library->GetPathName());
        Mutate->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
        Mutate->SetStringField(TEXT("expected_snapshot"), Snapshot);
        FUnrealMCPBlueprintMutator Mutator(Inspector);
        TestFalse(TEXT("library compile mutation denied"), Mutator.Execute(TEXT("blueprint_compile"), Mutate, Result, Error));
    }
    UBlueprint* Parent = CreateBlueprintFixture(Root + TEXT("/Parent"), AActor::StaticClass(), true);
    UBlueprint* Child = CreateBlueprintFixture(Root + TEXT("/Child"), Parent->GeneratedClass, false);
    USCS_Node* Node = Parent->SimpleConstructionScript->GetAllNodes().Last();
    auto* Handler = CastChecked<UBlueprintGeneratedClass>(Child->GeneratedClass)->GetInheritableComponentHandler(true);
    auto* Override = Cast<USceneComponent>(Handler->CreateOverridenComponentTemplate(FComponentKey(Node)));
    if (!TestNotNull(TEXT("child override template"), Override)) return false;
    Override->SetRelativeLocation(FVector(40, 50, 60));
    const auto Query = InspectArguments(Child->GetPathName());
    Query->SetStringField(TEXT("component_name"), Node->GetVariableName().ToString());
    Query->SetArrayField(TEXT("sections"), {MakeShared<FUnrealMCPValueString>(TEXT("components"))});
    Query->SetArrayField(TEXT("property_names"), {MakeShared<FUnrealMCPValueString>(TEXT("RelativeLocation"))});
    if (!TestTrue(TEXT("inherited component exact name"), Inspector.Execute(Query, Result, Error))) { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    const auto Component = Result->GetArrayField(TEXT("records"))[0]->AsObject();
    TestEqual(TEXT("effective child template"), Component->GetStringField(TEXT("template_path")), Override->GetPathName());
    const auto Value = Component->GetArrayField(TEXT("editable_properties"))[0]->AsObject();
    TestEqual(TEXT("property origin is child override"), Value->GetStringField(TEXT("property_origin")), Override->GetPathName());
    TestTrue(TEXT("child values encoded"), Value->GetStringField(TEXT("value")).Contains(TEXT("40")));
    Query->RemoveField(TEXT("component_name")); Query->SetStringField(TEXT("component_id"), Node->VariableGuid.ToString(EGuidFormats::Digits).ToLower());
    TestTrue(TEXT("ancestor stable component identity"), Inspector.Execute(Query, Result, Error));
    TestEqual(TEXT("component selectors share snapshot"), Result->GetStringField(TEXT("snapshot_id")), Snapshot);
    Override->SetRelativeLocation(FVector(70, 80, 90));
    TestTrue(TEXT("updated child component"), Inspector.Execute(Query, Result, Error));
    TestNotEqual(TEXT("child override changes snapshot"), Result->GetStringField(TEXT("snapshot_id")), Snapshot);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPGameplayAttributeReflectionTest,
    "UnrealMCP.AssetInspect.GameplayAttributeReflection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPGameplayAttributeReflectionTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::GameplayAttributeInspection;
    const FString Export = TEXT("(AttributeName=\"NetPriority\",Attribute=\"/Script/Engine.Actor:NetPriority\",AttributeOwner=\"/Script/CoreUObject.Class'/Script/Engine.Actor'\")");
    const auto Value = EncodeText(Export);
    TestTrue(TEXT("UE57 three-field export resolves"), Value->GetBoolField(TEXT("resolved")));
    TestTrue(TEXT("floating-point attribute compatible"), Value->GetBoolField(TEXT("compatible")));
    TestEqual(TEXT("owner path normalized"), Value->GetStringField(TEXT("owner_path")), FString(TEXT("/Script/Engine.Actor")));
    TestFalse(TEXT("missing property unresolved"), EncodeText(TEXT("(Attribute=\"/Script/Engine.Actor:Missing\")"))->GetBoolField(TEXT("resolved")));
    TestFalse(TEXT("bool property incompatible"), EncodeText(TEXT("(Attribute=\"/Script/Engine.Actor:bReplicates\")"))->GetBoolField(TEXT("compatible")));
    TestFalse(TEXT("conflicting metadata incompatible"), EncodeText(Export.Replace(TEXT("AttributeName=\"NetPriority\""), TEXT("AttributeName=\"Wrong\"")))->GetBoolField(TEXT("compatible")));
    TestFalse(TEXT("malformed export rejected"), EncodeText(TEXT("(Attribute=\"unterminated)"))->GetBoolField(TEXT("valid_export")));
    TestFalse(TEXT("duplicate fields rejected"), EncodeText(TEXT("(Attribute=None,Attribute=None)"))->GetBoolField(TEXT("valid_export")));
    TestFalse(TEXT("oversized export rejected"), EncodeText(FString::ChrN(4097, 'x'))->GetBoolField(TEXT("valid_export")));
    TestFalse(TEXT("empty attribute unresolved"), EncodeText(TEXT("()"))->GetBoolField(TEXT("resolved")));
    if (UScriptStruct* Type = FindObject<UScriptStruct>(nullptr, TEXT("/Script/GameplayAbilities.GameplayAttribute")))
    {
        FEdGraphPinType Pin; Pin.PinCategory = UEdGraphSchema_K2::PC_Struct; Pin.PinSubCategoryObject = Type;
        TestTrue(TEXT("K2 default typed attribute"), UnrealMCP::K2TypeCodec::EncodeDefault(Pin, Export)->GetBoolField(TEXT("resolved")));
        Pin.ContainerType = EPinContainerType::Array;
        TestEqual(TEXT("attribute array"), UnrealMCP::K2TypeCodec::EncodeDefault(Pin, TEXT("(") + Export + TEXT(")"))->GetArrayField(TEXT("items")).Num(), 1);
        Pin.ContainerType = EPinContainerType::Map;
        Pin.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Int;
        const auto Map = UnrealMCP::K2TypeCodec::EncodeDefault(Pin, TEXT("((") + Export + TEXT(",7))"));
        TestTrue(TEXT("attribute map key resolves"), Map->GetArrayField(TEXT("entries"))[0]->AsObject()->GetObjectField(TEXT("key"))->GetBoolField(TEXT("resolved")));
        FStructOnScope Storage(Type);
        const UScriptStruct* ModifierType = FindObject<UScriptStruct>(nullptr, TEXT("/Script/GameplayAbilities.GameplayModifierInfo"));
        FStructProperty* Property = ModifierType != nullptr ? FindFProperty<FStructProperty>(ModifierType, TEXT("Attribute")) : nullptr;
        if (!TestNotNull(TEXT("real reflected modifier attribute property"), Property)) return false;
        if (!TestNotNull(TEXT("native attribute import"), Property->ImportText_Direct(*Export, Storage.GetStructMemory(), nullptr, PPF_None))) return false;
        TSharedPtr<FUnrealMCPValue> Encoded;
        FUnrealMCPError Error;
        if (!TestTrue(TEXT("native reflected attribute encodes"), UnrealMCP::GameDataValueCodec::Encode(Property, Storage.GetStructMemory(), 0, Encoded, Error))) return false;
        if (!TestTrue(TEXT("native attribute object result"), Encoded.IsValid() && Encoded->AsObject().IsValid())) return false;
        TestTrue(TEXT("native reflected attribute resolves"), Encoded->AsObject()->GetBoolField(TEXT("resolved")));
        TestEqual(TEXT("native reflected attribute name"), Encoded->AsObject()->GetStringField(TEXT("attribute_name")), FString(TEXT("NetPriority")));
    }
    return true;
}
#endif
