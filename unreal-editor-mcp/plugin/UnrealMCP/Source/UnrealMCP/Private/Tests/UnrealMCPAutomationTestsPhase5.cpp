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

    FEdGraphPinType Unsupported;
    TestFalse(TEXT("unknown type rejects"), UnrealMCP::K2TypeCodec::DecodeType(K2Type(TEXT("wildcard")), Unsupported, Error));
    TestEqual(TEXT("unknown type error is stable"), Error.Code, FString(TEXT("unsupported_type")));
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

    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
    TestEqual(TEXT("member-edited Blueprint compiles without errors"), Log.NumErrors, 0);
    TestTrue(TEXT("member-edited Blueprint saves"), SaveBlueprintFixture(Blueprint));
    return true;
}


#endif
