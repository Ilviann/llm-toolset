#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

namespace
{
TSharedRef<FJsonObject> DataArguments(const FString& Target, const FString& Operation, const FString& AssetPath)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Result->SetStringField(TEXT("target"), Target); Result->SetStringField(TEXT("operation"), Operation);
    Result->SetStringField(TEXT("asset_path"), AssetPath); return Result;
}

TSharedRef<FJsonObject> StructMember(const FString& Name, const FString& Category, const TSharedPtr<FJsonObject>& Default)
{
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("name"), Name);
    Result->SetObjectField(TEXT("type"), UnrealMCP::Tests::K2Type(Category));
    if (Default.IsValid()) Result->SetObjectField(TEXT("default"), Default); return Result;
}

TSharedRef<FJsonObject> Row(const FString& Name, int32 Damage, const FString& Ammo)
{
    const TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>(); Values->SetNumberField(TEXT("Damage"), Damage); Values->SetStringField(TEXT("AmmoType"), Ammo);
    const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>(); Result->SetStringField(TEXT("row_name"), Name); Result->SetObjectField(TEXT("values"), Values); return Result;
}

bool Inspect(FUnrealMCPGameDataService& Service, const FString& Target, const FString& AssetPath, TSharedPtr<FJsonObject>& Out, FUnrealMCPError& Error, int32 PageSize = 100)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>(); Arguments->SetStringField(TEXT("target"), Target);
    Arguments->SetStringField(TEXT("asset_path"), AssetPath); Arguments->SetNumberField(TEXT("page_size"), PageSize);
    return Service.Inspect(Arguments, Out, Error);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase17GameDataAuthoringTest,
    "UnrealMCP.Phase17.GameDataAuthoring",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase17GameDataAuthoringTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString Prefix = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString StructPackage = Prefix + TEXT("/ST_WeaponStats");
    const FString TablePackage = Prefix + TEXT("/DT_WeaponStats");
    const FString NativeTablePackage = Prefix + TEXT("/DT_MirrorRows");
    FUnrealMCPGameDataService Service; TSharedPtr<FJsonObject> Result; FUnrealMCPError Error;

    TSharedRef<FJsonObject> CreateStruct = DataArguments(TEXT("user_defined_struct"), TEXT("create"), StructPackage);
    CreateStruct->SetArrayField(TEXT("members"), {
        MakeShared<FJsonValueObject>(StructMember(TEXT("Damage"), TEXT("int"), LiteralDefault(MakeShared<FJsonValueNumber>(25)))),
        MakeShared<FJsonValueObject>(StructMember(TEXT("AmmoType"), TEXT("string"), LiteralDefault(MakeShared<FJsonValueString>(TEXT("Rifle"))))) });
    if (!TestTrue(TEXT("weapon-stat schema creates, compiles, and saves"), Service.Edit(CreateStruct, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString StructPath = Result->GetStringField(TEXT("asset_path"));
    TestTrue(TEXT("user-defined struct snapshot is exact"), Result->GetStringField(TEXT("snapshot_id")).Len() == 40);

    if (!TestTrue(TEXT("weapon-stat schema inspects"), Inspect(Service, TEXT("user_defined_struct"), StructPath, Result, Error))) return false;
    TestEqual(TEXT("two identity-bearing schema members inspect"), Result->GetArrayField(TEXT("records")).Num(), 2);
    const FString StructSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    const FString DamageMember = Result->GetArrayField(TEXT("records"))[0]->AsObject()->GetStringField(TEXT("id"));

    TSharedRef<FJsonObject> Rename = DataArguments(TEXT("user_defined_struct"), TEXT("rename_member"), StructPath);
    Rename->SetStringField(TEXT("expected_snapshot"), StructSnapshot); Rename->SetStringField(TEXT("member_id"), DamageMember); Rename->SetStringField(TEXT("new_name"), TEXT("BaseDamage"));
    if (!TestTrue(TEXT("identity-based schema rename compiles and saves"), Service.Edit(Rename, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString RenamedSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    TestNotEqual(TEXT("schema rename changes snapshot"), RenamedSnapshot, StructSnapshot);
    TestFalse(TEXT("stale schema edit rejects"), Service.Edit(Rename, Result, Error));
    TestEqual(TEXT("stale schema edit code is stable"), Error.Code, FString(TEXT("stale_precondition")));

    TSharedRef<FJsonObject> CreateTable = DataArguments(TEXT("data_table"), TEXT("create"), TablePackage);
    CreateTable->SetStringField(TEXT("row_struct"), StructPath);
    const TSharedRef<FJsonObject> PistolValues = MakeShared<FJsonObject>(); PistolValues->SetNumberField(TEXT("BaseDamage"), 30); PistolValues->SetStringField(TEXT("AmmoType"), TEXT("Pistol"));
    const TSharedRef<FJsonObject> Pistol = MakeShared<FJsonObject>(); Pistol->SetStringField(TEXT("row_name"), TEXT("Pistol")); Pistol->SetObjectField(TEXT("values"), PistolValues);
    CreateTable->SetArrayField(TEXT("rows"), {MakeShared<FJsonValueObject>(Pistol)});
    if (!TestTrue(TEXT("typed weapon Data Table creates and saves"), Service.Edit(CreateTable, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString TablePath = Result->GetStringField(TEXT("asset_path"));

    if (!TestTrue(TEXT("typed Data Table inspects"), Inspect(Service, TEXT("data_table"), TablePath, Result, Error))) return false;
    TestEqual(TEXT("live row schema has two fields"), Result->GetArrayField(TEXT("schema")).Num(), 2);
    TestEqual(TEXT("created row reads back"), Result->GetArrayField(TEXT("records")).Num(), 1);
    FString TableSnapshot = Result->GetStringField(TEXT("snapshot_id"));

    if (!TestTrue(TEXT("dependent schema re-inspects"), Inspect(Service, TEXT("user_defined_struct"), StructPath, Result, Error))) return false;
    const FString DependentStructSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> RemoveMember = DataArguments(TEXT("user_defined_struct"), TEXT("remove_member"), StructPath);
    RemoveMember->SetStringField(TEXT("expected_snapshot"), DependentStructSnapshot); RemoveMember->SetStringField(TEXT("member_id"), DamageMember);
    RemoveMember->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    TestFalse(TEXT("dependent schema removal rejects"), Service.Edit(RemoveMember, Result, Error));
    TestEqual(TEXT("dependent schema removal has stable code"), Error.Code, FString(TEXT("referenced_schema")));
    if (!TestTrue(TEXT("rejected schema removal remains inspectable"), Inspect(Service, TEXT("user_defined_struct"), StructPath, Result, Error))) return false;
    TestEqual(TEXT("dependent schema rejection preserves snapshot"), Result->GetStringField(TEXT("snapshot_id")), DependentStructSnapshot);

    TSharedRef<FJsonObject> Add = DataArguments(TEXT("data_table"), TEXT("add_row"), TablePath); Add->SetStringField(TEXT("expected_snapshot"), TableSnapshot);
    Add->SetStringField(TEXT("row_name"), TEXT("Rifle")); const TSharedRef<FJsonObject> RifleValues = MakeShared<FJsonObject>(); RifleValues->SetNumberField(TEXT("BaseDamage"), 42); RifleValues->SetStringField(TEXT("AmmoType"), TEXT("Rifle")); Add->SetObjectField(TEXT("values"), RifleValues);
    if (!TestTrue(TEXT("typed row adds transactionally"), Service.Edit(Add, Result, Error))) return false; TableSnapshot = Result->GetStringField(TEXT("snapshot_id"));

    TSharedRef<FJsonObject> Batch = DataArguments(TEXT("data_table"), TEXT("batch"), TablePath); Batch->SetStringField(TEXT("expected_snapshot"), TableSnapshot);
    const TSharedRef<FJsonObject> RiflePatchValues = MakeShared<FJsonObject>(); RiflePatchValues->SetNumberField(TEXT("BaseDamage"), 45);
    const TSharedRef<FJsonObject> RiflePatch = MakeShared<FJsonObject>(); RiflePatch->SetStringField(TEXT("row_name"), TEXT("Rifle")); RiflePatch->SetObjectField(TEXT("values"), RiflePatchValues); RiflePatch->SetBoolField(TEXT("preserve_unspecified"), true);
    Batch->SetArrayField(TEXT("upserts"), {MakeShared<FJsonValueObject>(RiflePatch)}); Batch->SetArrayField(TEXT("remove_rows"), {MakeShared<FJsonValueString>(TEXT("Pistol"))});
    if (!TestTrue(TEXT("mixed upsert/remove batch commits atomically"), Service.Edit(Batch, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TableSnapshot = Result->GetStringField(TEXT("snapshot_id"));

    if (!TestTrue(TEXT("batch result inspects"), Inspect(Service, TEXT("data_table"), TablePath, Result, Error, 1))) return false;
    TestEqual(TEXT("batch removed one row and retained one"), static_cast<int32>(Result->GetObjectField(TEXT("metadata"))->GetNumberField(TEXT("row_count"))), 1);
    const TSharedPtr<FJsonObject> Rifle = Result->GetArrayField(TEXT("records"))[0]->AsObject();
    TestEqual(TEXT("preserved row field survives patch"), Rifle->GetObjectField(TEXT("values"))->GetStringField(TEXT("AmmoType")), FString(TEXT("Rifle")));
    TestEqual(TEXT("patched numeric field reads back"), static_cast<int32>(Rifle->GetObjectField(TEXT("values"))->GetNumberField(TEXT("BaseDamage"))), 45);

    TSharedRef<FJsonObject> InvalidBatch = DataArguments(TEXT("data_table"), TEXT("batch"), TablePath); InvalidBatch->SetStringField(TEXT("expected_snapshot"), TableSnapshot);
    InvalidBatch->SetArrayField(TEXT("upserts"), {MakeShared<FJsonValueObject>(Row(TEXT("Rifle"), 1, TEXT("Rifle")))});
    InvalidBatch->SetArrayField(TEXT("remove_rows"), {MakeShared<FJsonValueString>(TEXT("rifle"))});
    TestFalse(TEXT("case-conflicting batch rejects without partial work"), Service.Edit(InvalidBatch, Result, Error));
    TestEqual(TEXT("case-conflicting batch has stable code"), Error.Code, FString(TEXT("invalid_row")));
    if (!TestTrue(TEXT("rejected batch remains inspectable"), Inspect(Service, TEXT("data_table"), TablePath, Result, Error))) return false;
    TestEqual(TEXT("rejected batch preserves snapshot"), Result->GetStringField(TEXT("snapshot_id")), TableSnapshot);

    TSharedRef<FJsonObject> CreateNativeTable = DataArguments(TEXT("data_table"), TEXT("create"), NativeTablePackage);
    CreateNativeTable->SetStringField(TEXT("row_struct"), TEXT("/Script/Engine.MirrorTableRow"));
    const TSharedRef<FJsonObject> NativeValues = MakeShared<FJsonObject>(); NativeValues->SetStringField(TEXT("Name"), TEXT("hand_l"));
    NativeValues->SetStringField(TEXT("MirroredName"), TEXT("hand_r")); NativeValues->SetBoolField(TEXT("bEnabled"), true);
    const TSharedRef<FJsonObject> NativeRow = MakeShared<FJsonObject>(); NativeRow->SetStringField(TEXT("row_name"), TEXT("LeftHand")); NativeRow->SetObjectField(TEXT("values"), NativeValues);
    CreateNativeTable->SetArrayField(TEXT("rows"), {MakeShared<FJsonValueObject>(NativeRow)});
    if (!TestTrue(TEXT("native row-struct Data Table creates and saves"), Service.Edit(CreateNativeTable, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString NativeTablePath = Result->GetStringField(TEXT("asset_path"));
    if (!TestTrue(TEXT("native row-struct Data Table inspects"), Inspect(Service, TEXT("data_table"), NativeTablePath, Result, Error))) return false;
    TestEqual(TEXT("native row-struct kind is explicit"), Result->GetObjectField(TEXT("metadata"))->GetStringField(TEXT("row_struct_kind")), FString(TEXT("native")));
    const TSharedPtr<FJsonObject> NativeReadBack = Result->GetArrayField(TEXT("records"))[0]->AsObject()->GetObjectField(TEXT("values"));
    TestEqual(TEXT("native name value reads back"), NativeReadBack->GetStringField(TEXT("Name")), FString(TEXT("hand_l")));
    TestEqual(TEXT("native mirrored-name value reads back"), NativeReadBack->GetStringField(TEXT("MirroredName")), FString(TEXT("hand_r")));
    return true;
}

#endif
