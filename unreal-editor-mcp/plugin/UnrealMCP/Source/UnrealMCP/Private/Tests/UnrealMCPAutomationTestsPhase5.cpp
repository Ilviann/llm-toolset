#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase5K2TypeCodecTest, "UnrealMCP.Phase5.K2TypeCodec", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase5K2TypeCodecTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    struct FTypeCase { FString Category; FString Subcategory; FString TypeObject; };
    const TArray<FTypeCase> Cases = {
        {TEXT("boolean"), FString(), FString()}, {TEXT("byte"), FString(), FString()},
        {TEXT("int"), FString(), FString()}, {TEXT("int64"), FString(), FString()},
        {TEXT("real"), TEXT("float"), FString()}, {TEXT("real"), TEXT("double"), FString()},
        {TEXT("name"), FString(), FString()}, {TEXT("string"), FString(), FString()},
        {TEXT("text"), FString(), FString()},
        {TEXT("enum"), FString(), StaticEnum<ECollisionChannel>()->GetPathName()},
        {TEXT("struct"), FString(), TBaseStructure<FVector>::Get()->GetPathName()},
        {TEXT("object"), FString(), UTexture2D::StaticClass()->GetPathName()},
        {TEXT("class"), FString(), AActor::StaticClass()->GetPathName()},
        {TEXT("softobject"), FString(), UTexture2D::StaticClass()->GetPathName()},
        {TEXT("softclass"), FString(), AActor::StaticClass()->GetPathName()},
    };
    for (const FTypeCase& Case : Cases)
    {
        const TSharedRef<FJsonObject> Json = K2Type(Case.Category);
        if (!Case.Subcategory.IsEmpty()) Json->SetStringField(TEXT("subcategory"), Case.Subcategory);
        if (!Case.TypeObject.IsEmpty()) Json->SetStringField(TEXT("type_object"), Case.TypeObject);
        FEdGraphPinType Type;
        FUnrealMCPError Error;
        const bool bDecoded = UnrealMCP::K2TypeCodec::DecodeType(Json, Type, Error);
        if (!bDecoded) AddError(Case.Category + TEXT(": ") + Error.Code + TEXT(": ") + Error.Message);
        TestTrue(*FString::Printf(TEXT("%s type decodes"), *Case.Category), bDecoded);
        if (bDecoded) TestTrue(*FString::Printf(TEXT("%s type reports supported"), *Case.Category),
            UnrealMCP::K2TypeCodec::EncodeType(Type)->GetBoolField(TEXT("supported")));
    }

    FUnrealMCPError Error;
    FEdGraphPinType ArrayType;
    const TSharedRef<FJsonObject> ArrayJson = K2Type(TEXT("string"), TEXT("array"));
    TestTrue(TEXT("array type decodes"), UnrealMCP::K2TypeCodec::DecodeType(ArrayJson, ArrayType, Error));
    const TSharedRef<FJsonObject> ArrayDefault = MakeShared<FJsonObject>();
    ArrayDefault->SetStringField(TEXT("kind"), TEXT("array"));
    ArrayDefault->SetArrayField(TEXT("items"), {
        MakeShared<FJsonValueObject>(LiteralDefault(MakeShared<FJsonValueString>(TEXT("Alpha")))),
        MakeShared<FJsonValueObject>(LiteralDefault(MakeShared<FJsonValueString>(TEXT("Beta"))))});
    FString Encoded;
    TestTrue(TEXT("array default decodes"), UnrealMCP::K2TypeCodec::DecodeDefault(ArrayType, ArrayDefault, Encoded, Error));
    TestEqual(TEXT("array default has canonical bounded text"), Encoded, FString(TEXT("(\"Alpha\",\"Beta\")")));
    TestEqual(TEXT("array default round trips two explicit items"),
        UnrealMCP::K2TypeCodec::EncodeDefault(ArrayType, Encoded)->GetArrayField(TEXT("items")).Num(), 2);

    FEdGraphPinType MapType;
    const TSharedRef<FJsonObject> MapJson = K2Type(TEXT("name"), TEXT("map"));
    MapJson->SetObjectField(TEXT("value_type"), K2Type(TEXT("int")));
    MapJson->GetObjectField(TEXT("value_type"))->RemoveField(TEXT("container"));
    TestTrue(TEXT("map type decodes"), UnrealMCP::K2TypeCodec::DecodeType(MapJson, MapType, Error));
    const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
    Entry->SetObjectField(TEXT("key"), LiteralDefault(MakeShared<FJsonValueString>(TEXT("Score"))));
    Entry->SetObjectField(TEXT("value"), LiteralDefault(MakeShared<FJsonValueNumber>(7)));
    const TSharedRef<FJsonObject> MapDefault = MakeShared<FJsonObject>();
    MapDefault->SetStringField(TEXT("kind"), TEXT("map"));
    MapDefault->SetArrayField(TEXT("entries"), {MakeShared<FJsonValueObject>(Entry)});
    TestTrue(TEXT("map default decodes"), UnrealMCP::K2TypeCodec::DecodeDefault(MapType, MapDefault, Encoded, Error));
    TestEqual(TEXT("map default has canonical bounded text"), Encoded, FString(TEXT("((\"Score\",7))")));

    UScriptStruct* GameplayAttributeStruct = LoadObject<UScriptStruct>(
        nullptr, TEXT("/Script/GameplayAbilities.GameplayAttribute"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    UClass* AttributeSetClass = LoadObject<UClass>(
        nullptr, TEXT("/Script/GameplayAbilities.AbilitySystemTestAttributeSet"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    FProperty* HealthProperty = AttributeSetClass != nullptr
        ? AttributeSetClass->FindPropertyByName(TEXT("Health")) : nullptr;
    FProperty* ManaProperty = AttributeSetClass != nullptr
        ? AttributeSetClass->FindPropertyByName(TEXT("Mana")) : nullptr;
    if (TestNotNull(TEXT("Gameplay Attribute K2 type is available"), GameplayAttributeStruct)
        && TestNotNull(TEXT("Gameplay Attribute owner fixture is available"), AttributeSetClass)
        && TestNotNull(TEXT("legacy float Gameplay Attribute fixture is available"), HealthProperty)
        && TestNotNull(TEXT("Gameplay Attribute Data fixture is available"), ManaProperty))
    {
        FEdGraphPinType GameplayAttributeType;
        GameplayAttributeType.PinCategory = UEdGraphSchema_K2::PC_Struct;
        GameplayAttributeType.PinSubCategoryObject = GameplayAttributeStruct;
        const FString AttributeText = FString::Printf(TEXT("(Attribute=%s,AttributeOwner=%s)"),
            *HealthProperty->GetPathName(), *AttributeSetClass->GetPathName());
        const TSharedRef<FJsonObject> AttributeDefault = UnrealMCP::K2TypeCodec::EncodeDefault(
            GameplayAttributeType, AttributeText);
        TestEqual(TEXT("Gameplay Attribute default has a typed kind"),
            AttributeDefault->GetStringField(TEXT("kind")), FString(TEXT("gameplay_attribute")));
        TestTrue(TEXT("Gameplay Attribute default resolves"), AttributeDefault->GetBoolField(TEXT("resolved")));
        TestTrue(TEXT("Gameplay Attribute default is compatible"), AttributeDefault->GetBoolField(TEXT("compatible")));
        TestEqual(TEXT("Gameplay Attribute default exposes its name"),
            AttributeDefault->GetStringField(TEXT("name")), FString(TEXT("Health")));
        TestEqual(TEXT("Gameplay Attribute default exposes its property path"),
            AttributeDefault->GetStringField(TEXT("property_path")), HealthProperty->GetPathName());
        const FString DataAttributeText = FString::Printf(TEXT("(Attribute=%s,AttributeOwner=%s)"),
            *ManaProperty->GetPathName(), *AttributeSetClass->GetPathName());
        TestTrue(TEXT("Gameplay Attribute Data default is compatible"),
            UnrealMCP::K2TypeCodec::EncodeDefault(GameplayAttributeType, DataAttributeText)->GetBoolField(TEXT("compatible")));
        TestEqual(TEXT("malformed Gameplay Attribute defaults remain explicit"),
            UnrealMCP::K2TypeCodec::EncodeDefault(GameplayAttributeType, TEXT("invalid"))->GetStringField(TEXT("kind")),
            FString(TEXT("unavailable")));
    }

    FEdGraphPinType Unsupported;
    TestFalse(TEXT("unknown type rejects"), UnrealMCP::K2TypeCodec::DecodeType(K2Type(TEXT("wildcard")), Unsupported, Error));
    TestEqual(TEXT("unknown type error is stable"), Error.Code, FString(TEXT("unsupported_type")));
    Unsupported.PinCategory = TEXT("unreal_mcp_unsupported");
    TestEqual(TEXT("unsupported empty default is unavailable"),
        UnrealMCP::K2TypeCodec::EncodeDefault(Unsupported, FString())->GetStringField(TEXT("kind")), FString(TEXT("unavailable")));
    TestEqual(TEXT("unsupported populated default is unavailable"),
        UnrealMCP::K2TypeCodec::EncodeDefault(Unsupported, TEXT("legacy"))->GetStringField(TEXT("kind")), FString(TEXT("unavailable")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase5MemberVariableTest, "UnrealMCP.Phase5.MemberVariables", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase5MemberVariableTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Phase5");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), false);
    if (!TestNotNull(TEXT("Phase 5 Blueprint fixture is created"), Blueprint)) return false;
    const FString AssetPath = Blueprint->GetPathName();
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    FString Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> Add = MemberEditArguments(AssetPath, Snapshot, TEXT("add"));
    Add->SetStringField(TEXT("name"), TEXT("Health"));
    Add->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
    Add->SetObjectField(TEXT("default"), LiteralDefault(MakeShared<FJsonValueNumber>(100)));
    const TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
    Metadata->SetStringField(TEXT("category"), TEXT("Stats"));
    Metadata->SetStringField(TEXT("tooltip"), TEXT("Current health"));
    Metadata->SetBoolField(TEXT("instance_editable"), true);
    Metadata->SetBoolField(TEXT("blueprint_visible"), true);
    Metadata->SetStringField(TEXT("replication"), TEXT("replicated"));
    Add->SetObjectField(TEXT("metadata"), Metadata);
    if (!TestTrue(TEXT("typed member add succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), Add, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    FString HealthId = MemberIdByName(Inspector, AssetPath, TEXT("Health"));
    TestEqual(TEXT("added member gets stable identity"), HealthId.Len(), 32);
    TestTrue(TEXT("new member is initially unreferenced"), !Result->GetObjectField(TEXT("reference_summary"))->GetBoolField(TEXT("referenced")));
    TestEqual(TEXT("member default reads back exactly"),
        Result->GetObjectField(TEXT("member"))->GetObjectField(TEXT("default"))->GetNumberField(TEXT("value")), 100.0);
    TestEqual(TEXT("member category reads back exactly"),
        Result->GetObjectField(TEXT("member"))->GetObjectField(TEXT("metadata"))->GetStringField(TEXT("category")), FString(TEXT("Stats")));
    TestEqual(TEXT("member replication reads back exactly"),
        Result->GetObjectField(TEXT("member"))->GetObjectField(TEXT("replication"))->GetStringField(TEXT("mode")), FString(TEXT("replicated")));

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> TargetedInspect = InspectArguments(AssetPath);
    TargetedInspect->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("variables"))});
    TargetedInspect->SetStringField(TEXT("member_id"), HealthId);
    if (TestTrue(TEXT("stable member identity supports exact inspection"), Inspector.Execute(TargetedInspect, Result, Error)))
    {
        TestEqual(TEXT("targeted member inspection retains the authoritative snapshot"), Result->GetStringField(TEXT("snapshot_id")), Snapshot);
    }
    TSharedRef<FJsonObject> Duplicate = MemberEditArguments(AssetPath, Snapshot, TEXT("add"));
    Duplicate->SetStringField(TEXT("name"), TEXT("Health"));
    Duplicate->SetObjectField(TEXT("type"), K2Type(TEXT("boolean")));
    TestFalse(TEXT("duplicate member name rejects"), Mutator.Execute(TEXT("blueprint_member_edit"), Duplicate, Result, Error));
    TestEqual(TEXT("duplicate error is stable"), Error.Code, FString(TEXT("invalid_member")));
    TestEqual(TEXT("duplicate rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> InheritedCollision = MemberEditArguments(AssetPath, Snapshot, TEXT("add"));
    InheritedCollision->SetStringField(TEXT("name"), TEXT("InitialLifeSpan"));
    InheritedCollision->SetObjectField(TEXT("type"), K2Type(TEXT("real")));
    InheritedCollision->GetObjectField(TEXT("type"))->SetStringField(TEXT("subcategory"), TEXT("float"));
    TestFalse(TEXT("inherited member collision rejects"), Mutator.Execute(TEXT("blueprint_member_edit"), InheritedCollision, Result, Error));
    TestEqual(TEXT("inherited collision preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> GraphCollision = MemberEditArguments(AssetPath, Snapshot, TEXT("add"));
    GraphCollision->SetStringField(TEXT("name"), TEXT("EventGraph"));
    GraphCollision->SetObjectField(TEXT("type"), K2Type(TEXT("boolean")));
    TestFalse(TEXT("cross-kind graph collision rejects"), Mutator.Execute(TEXT("blueprint_member_edit"), GraphCollision, Result, Error));
    TestEqual(TEXT("cross-kind rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> Rename = MemberEditArguments(AssetPath, Snapshot, TEXT("rename"));
    Rename->SetStringField(TEXT("member_id"), HealthId);
    Rename->SetStringField(TEXT("new_name"), TEXT("HitPoints"));
    if (!TestTrue(TEXT("member rename succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), Rename, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("rename preserves stable identity"), MemberIdByName(Inspector, AssetPath, TEXT("HitPoints")), HealthId);

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> UpdateDefault = MemberEditArguments(AssetPath, Snapshot, TEXT("update"));
    UpdateDefault->SetStringField(TEXT("member_id"), HealthId);
    UpdateDefault->SetStringField(TEXT("field"), TEXT("default"));
    UpdateDefault->SetObjectField(TEXT("default"), LiteralDefault(MakeShared<FJsonValueNumber>(125)));
    if (!TestTrue(TEXT("member default update succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), UpdateDefault, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("updated default reads back exactly"),
        Result->GetObjectField(TEXT("member"))->GetObjectField(TEXT("default"))->GetNumberField(TEXT("value")), 125.0);

    const FString BeforeMetadata = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> UpdateMetadata = MemberEditArguments(AssetPath, BeforeMetadata, TEXT("update"));
    UpdateMetadata->SetStringField(TEXT("member_id"), HealthId);
    UpdateMetadata->SetStringField(TEXT("field"), TEXT("metadata"));
    const TSharedRef<FJsonObject> MetadataChanges = MakeShared<FJsonObject>();
    MetadataChanges->SetStringField(TEXT("category"), TEXT("Combat"));
    MetadataChanges->SetBoolField(TEXT("save_game"), true);
    MetadataChanges->SetBoolField(TEXT("blueprint_read_only"), true);
    UpdateMetadata->SetObjectField(TEXT("metadata"), MetadataChanges);
    if (!TestTrue(TEXT("member metadata update succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), UpdateMetadata, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString AfterMetadata = Result->GetStringField(TEXT("snapshot_id"));
    TestTrue(TEXT("member transaction undoes"), GEditor != nullptr && GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores prior member snapshot"), InspectSnapshot(Inspector, AssetPath), BeforeMetadata);
    TestTrue(TEXT("member transaction redoes"), GEditor != nullptr && GEditor->RedoTransaction());
    TestEqual(TEXT("Redo restores edited member snapshot"), InspectSnapshot(Inspector, AssetPath), AfterMetadata);

    Snapshot = AfterMetadata;
    TSharedRef<FJsonObject> AddReferenced = MemberEditArguments(AssetPath, Snapshot, TEXT("add"));
    AddReferenced->SetStringField(TEXT("name"), TEXT("Referenced"));
    AddReferenced->SetObjectField(TEXT("type"), K2Type(TEXT("boolean")));
    if (!TestTrue(TEXT("reference fixture member add succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), AddReferenced, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString ReferencedId = MemberIdByName(Inspector, AssetPath, TEXT("Referenced"));
    FBPVariableDescription* ReferencedVariable = nullptr;
    for (FBPVariableDescription& Candidate : Blueprint->NewVariables)
    {
        if (Candidate.VarGuid.ToString(EGuidFormats::Digits).ToLower() == ReferencedId) ReferencedVariable = &Candidate;
    }
    UEdGraph* EventGraph = !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("event graph exists for reference fixture"), EventGraph) || !TestNotNull(TEXT("referenced variable exists"), ReferencedVariable)) return false;
    EventGraph->Modify();
    UK2Node_VariableGet* Getter = NewObject<UK2Node_VariableGet>(EventGraph);
    Getter->VariableReference.SetSelfMember(ReferencedVariable->VarName, ReferencedVariable->VarGuid);
    Getter->CreateNewGuid();
    EventGraph->AddNode(Getter, true, false);
    Getter->PostPlacedNewNode();
    Getter->AllocateDefaultPins();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Snapshot = InspectSnapshot(Inspector, AssetPath);

    TSharedRef<FJsonObject> RemoveReferenced = MemberEditArguments(AssetPath, Snapshot, TEXT("remove"));
    RemoveReferenced->SetStringField(TEXT("member_id"), ReferencedId);
    RemoveReferenced->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    TestFalse(TEXT("referenced member removal rejects"), Mutator.Execute(TEXT("blueprint_member_edit"), RemoveReferenced, Result, Error));
    TestEqual(TEXT("referenced removal error is stable"), Error.Code, FString(TEXT("referenced_member")));
    TestEqual(TEXT("referenced removal preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> ChangeReferencedType = MemberEditArguments(AssetPath, Snapshot, TEXT("update"));
    ChangeReferencedType->SetStringField(TEXT("member_id"), ReferencedId);
    ChangeReferencedType->SetStringField(TEXT("field"), TEXT("type"));
    ChangeReferencedType->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    ChangeReferencedType->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
    TestFalse(TEXT("referenced member type change rejects"), Mutator.Execute(TEXT("blueprint_member_edit"), ChangeReferencedType, Result, Error));
    TestEqual(TEXT("referenced type rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    FBPVariableDescription* HitPoints = nullptr;
    for (FBPVariableDescription& Candidate : Blueprint->NewVariables)
    {
        if (Candidate.VarGuid.ToString(EGuidFormats::Digits).ToLower() == HealthId) HitPoints = &Candidate;
    }
    if (!TestNotNull(TEXT("renamed member exists for RepNotify fixture"), HitPoints)) return false;
    HitPoints->RepNotifyFunc = TEXT("OnRep_HitPoints");
    HitPoints->PropertyFlags |= CPF_Net | CPF_RepNotify;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> RepNotifyInspect = InspectArguments(AssetPath);
    RepNotifyInspect->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("variables"))});
    RepNotifyInspect->SetStringField(TEXT("member_id"), HealthId);
    TestTrue(TEXT("RepNotify member inspection succeeds"), Inspector.Execute(RepNotifyInspect, Result, Error));
    TestEqual(TEXT("RepNotify relationship is exposed"),
        Result->GetArrayField(TEXT("records"))[0]->AsObject()->GetObjectField(TEXT("replication"))->GetStringField(TEXT("rep_notify_function")),
        FString(TEXT("OnRep_HitPoints")));
    TestFalse(TEXT("invalid legacy RepNotify relationship is identified"),
        Result->GetArrayField(TEXT("records"))[0]->AsObject()->GetObjectField(TEXT("replication"))->GetBoolField(TEXT("relationship_valid")));
    HitPoints->RepNotifyFunc = NAME_None;
    HitPoints->PropertyFlags &= ~static_cast<uint64>(CPF_RepNotify);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    FEdGraphPinType ClassType;
    ClassType.PinCategory = UEdGraphSchema_K2::PC_Class;
    ClassType.PinSubCategoryObject = AActor::StaticClass();
    TestTrue(TEXT("Blueprint-owned class-reference member is added"), FBlueprintEditorUtils::AddMemberVariable(
        Blueprint, TEXT("ActorClass"), ClassType, AActor::StaticClass()->GetPathName()));

    UScriptStruct* GameplayAttributeStruct = LoadObject<UScriptStruct>(
        nullptr, TEXT("/Script/GameplayAbilities.GameplayAttribute"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    UClass* AttributeSetClass = LoadObject<UClass>(
        nullptr, TEXT("/Script/GameplayAbilities.AbilitySystemTestAttributeSet"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    FProperty* HealthProperty = AttributeSetClass != nullptr
        ? AttributeSetClass->FindPropertyByName(TEXT("Health")) : nullptr;
    if (!TestNotNull(TEXT("Gameplay Attribute member type is available"), GameplayAttributeStruct)
        || !TestNotNull(TEXT("Gameplay Attribute member owner is available"), AttributeSetClass)
        || !TestNotNull(TEXT("Gameplay Attribute member target is available"), HealthProperty)) return false;
    FEdGraphPinType GameplayAttributeType;
    GameplayAttributeType.PinCategory = UEdGraphSchema_K2::PC_Struct;
    GameplayAttributeType.PinSubCategoryObject = GameplayAttributeStruct;
    const FString GameplayAttributeDefault = FString::Printf(TEXT("(Attribute=%s,AttributeOwner=%s)"),
        *HealthProperty->GetPathName(), *AttributeSetClass->GetPathName());
    TestTrue(TEXT("Gameplay Attribute member is added"), FBlueprintEditorUtils::AddMemberVariable(
        Blueprint, TEXT("ObservedAttribute"), GameplayAttributeType, GameplayAttributeDefault));
    for (FBPVariableDescription& Candidate : Blueprint->NewVariables)
    {
        if (Candidate.VarName == TEXT("ObservedAttribute")) Candidate.PropertyFlags |= CPF_Edit | CPF_BlueprintVisible;
    }
    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
    TestEqual(TEXT("member-edited Blueprint compiles without errors"), Log.NumErrors, 0);
    const FString ActorClassId = MemberIdByName(Inspector, AssetPath, TEXT("ActorClass"));
    TSharedRef<FJsonObject> ClassInspect = InspectArguments(AssetPath);
    ClassInspect->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("variables"))});
    ClassInspect->SetStringField(TEXT("member_id"), ActorClassId);
    if (TestTrue(TEXT("Blueprint-owned class-reference member inspection is safe"),
        Inspector.Execute(ClassInspect, Result, Error)))
    {
        TestEqual(TEXT("class-reference default reads back exactly"),
            Result->GetArrayField(TEXT("records"))[0]->AsObject()->GetObjectField(TEXT("default"))->GetStringField(TEXT("path")),
            AActor::StaticClass()->GetPathName());
    }
    const FString GameplayAttributeId = MemberIdByName(Inspector, AssetPath, TEXT("ObservedAttribute"));
    TSharedRef<FJsonObject> GameplayAttributeInspect = InspectArguments(AssetPath);
    GameplayAttributeInspect->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("variables"))});
    GameplayAttributeInspect->SetStringField(TEXT("member_id"), GameplayAttributeId);
    if (TestTrue(TEXT("Gameplay Attribute member inspection succeeds"),
        Inspector.Execute(GameplayAttributeInspect, Result, Error)))
    {
        const TSharedPtr<FJsonObject> Default = Result->GetArrayField(TEXT("records"))[0]->AsObject()->GetObjectField(TEXT("default"));
        TestEqual(TEXT("Gameplay Attribute member uses its typed value"),
            Default->GetStringField(TEXT("kind")), FString(TEXT("gameplay_attribute")));
        TestEqual(TEXT("Gameplay Attribute member exposes its property"),
            Default->GetStringField(TEXT("property_path")), HealthProperty->GetPathName());
    }
    TSharedRef<FJsonObject> GameplayAttributeClassDefault = InspectArguments(AssetPath);
    GameplayAttributeClassDefault->SetArrayField(
        TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("class_defaults"))});
    GameplayAttributeClassDefault->SetArrayField(
        TEXT("property_names"), {MakeShared<FJsonValueString>(TEXT("ObservedAttribute"))});
    if (TestTrue(TEXT("Gameplay Attribute class-default inspection succeeds"),
        Inspector.Execute(GameplayAttributeClassDefault, Result, Error)))
    {
        const TSharedPtr<FJsonObject> Default = Result->GetArrayField(TEXT("records"))[0]->AsObject();
        TestEqual(TEXT("Gameplay Attribute class default has a typed property kind"),
            Default->GetStringField(TEXT("type")), FString(TEXT("gameplay_attribute")));
        TestEqual(TEXT("Gameplay Attribute class default exposes its property"),
            Default->GetObjectField(TEXT("value"))->GetStringField(TEXT("property_path")), HealthProperty->GetPathName());
    }
    TestTrue(TEXT("member-edited Blueprint saves"), SaveBlueprintFixture(Blueprint));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPPhase5GameplayEffectModifiersTest,
    "UnrealMCP.Phase5.GameplayEffectModifiers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase5GameplayEffectModifiersTest::RunTest(const FString& Parameters)
{
    UClass* GameplayEffectClass = LoadObject<UClass>(
        nullptr, TEXT("/Script/GameplayAbilities.GameplayEffect"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (!TestNotNull(TEXT("Gameplay Effect class is available"), GameplayEffectClass)) return false;

    UObject* Effect = NewObject<UObject>(GetTransientPackage(), GameplayEffectClass);
    FArrayProperty* ModifiersProperty = CastField<FArrayProperty>(
        GameplayEffectClass->FindPropertyByName(TEXT("Modifiers")));
    FStructProperty* ModifierProperty = ModifiersProperty != nullptr
        ? CastField<FStructProperty>(ModifiersProperty->Inner) : nullptr;
    if (!TestNotNull(TEXT("Gameplay Effect Modifiers array is available"), ModifiersProperty)
        || !TestNotNull(TEXT("Gameplay Effect modifier element is a struct"), ModifierProperty)) return false;

    FScriptArrayHelper Modifiers(ModifiersProperty, ModifiersProperty->ContainerPtrToValuePtr<void>(Effect));
    const int32 ModifierIndex = Modifiers.AddValue();
    void* Modifier = Modifiers.GetRawPtr(ModifierIndex);
    FStructProperty* AttributeProperty = CastField<FStructProperty>(
        ModifierProperty->Struct->FindPropertyByName(TEXT("Attribute")));
    UClass* AttributeSetClass = LoadObject<UClass>(
        nullptr, TEXT("/Script/GameplayAbilities.AbilitySystemTestAttributeSet"), nullptr, LOAD_NoWarn | LOAD_Quiet);
    FProperty* HealthProperty = AttributeSetClass != nullptr
        ? AttributeSetClass->FindPropertyByName(TEXT("Health")) : nullptr;
    if (!TestNotNull(TEXT("modifier Gameplay Attribute field is available"), AttributeProperty)
        || !TestNotNull(TEXT("modifier Attribute Set fixture is available"), AttributeSetClass)
        || !TestNotNull(TEXT("modifier attribute fixture is available"), HealthProperty)) return false;
    const FString AttributeText = FString::Printf(TEXT("(Attribute=%s,AttributeOwner=%s)"),
        *HealthProperty->GetPathName(), *AttributeSetClass->GetPathName());
    TestNotNull(TEXT("modifier Gameplay Attribute value imports"), AttributeProperty->ImportText_Direct(
        *AttributeText, AttributeProperty->ContainerPtrToValuePtr<void>(Modifier), Effect, PPF_None));

    const TSharedRef<FJsonObject> Encoded = UnrealMCP::PropertyCodec::Encode(Effect, ModifiersProperty);
    TestTrue(TEXT("Gameplay Effect Modifiers array is supported"), Encoded->GetBoolField(TEXT("supported")));
    TestEqual(TEXT("Gameplay Effect Modifiers keeps the array type"),
        Encoded->GetStringField(TEXT("type")), FString(TEXT("array")));
    const TArray<TSharedPtr<FJsonValue>> Values = Encoded->GetArrayField(TEXT("value"));
    TestEqual(TEXT("Gameplay Effect Modifiers returns every bounded element"), Values.Num(), 1);
    if (Values.Num() == 1)
    {
        const TSharedPtr<FJsonObject> ModifierValue = Values[0]->AsObject();
        TestEqual(TEXT("modifier uses the bounded struct encoding"),
            ModifierValue->GetStringField(TEXT("kind")), FString(TEXT("struct")));
        const TSharedPtr<FJsonObject> Fields = ModifierValue->GetObjectField(TEXT("fields"));
        const TSharedPtr<FJsonObject> Attribute = Fields->GetObjectField(TEXT("Attribute"));
        TestEqual(TEXT("modifier attribute keeps its typed encoding"),
            Attribute->GetStringField(TEXT("kind")), FString(TEXT("gameplay_attribute")));
        TestEqual(TEXT("modifier attribute resolves exactly"),
            Attribute->GetStringField(TEXT("property_path")), HealthProperty->GetPathName());
        const TSharedPtr<FJsonObject> Magnitude = Fields->GetObjectField(TEXT("ModifierMagnitude"));
        TestTrue(TEXT("modifier magnitude fields are inspectable"),
            Magnitude->GetObjectField(TEXT("fields"))->HasField(TEXT("ScalableFloatMagnitude")));
        TestTrue(TEXT("modifier source requirements are inspectable"),
            Fields->HasField(TEXT("SourceTags")));
        TestTrue(TEXT("modifier target requirements are inspectable"),
            Fields->HasField(TEXT("TargetTags")));
    }
    return true;
}


#endif
