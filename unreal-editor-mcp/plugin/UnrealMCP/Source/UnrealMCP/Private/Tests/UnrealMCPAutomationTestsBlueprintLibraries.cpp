#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet/BlueprintFunctionLibrary.h"

namespace
{
UBlueprint* CreateLibraryFixture(
    const FString& PackageName,
    UClass* ParentClass,
    EBlueprintType BlueprintType,
    const FName GraphName)
{
    UPackage* Package = CreatePackage(*PackageName);
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        ParentClass, Package, FName(*AssetName), BlueprintType,
        UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(),
        FName(TEXT("UnrealMCP.BlueprintLibraries")));
    if (Blueprint == nullptr)
    {
        return nullptr;
    }
    FAssetRegistryModule::AssetCreated(Blueprint);
    UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(
        Blueprint, GraphName, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
    if (Graph == nullptr)
    {
        return nullptr;
    }
    if (BlueprintType == BPTYPE_FunctionLibrary)
    {
        FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);
    }
    else
    {
        FBlueprintEditorUtils::AddMacroGraph(Blueprint, Graph, true, nullptr);
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Blueprint;
}

TSharedPtr<FJsonObject> FindRecord(
    const TSharedPtr<FJsonObject>& Result,
    const FString& Section,
    const FString& Name = FString())
{
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr)
    {
        return nullptr;
    }
    for (const TSharedPtr<FJsonValue>& Item : *Records)
    {
        const TSharedPtr<FJsonObject> Record = Item.IsValid() ? Item->AsObject() : nullptr;
        if (!Record.IsValid() || Record->GetStringField(TEXT("section")) != Section)
        {
            continue;
        }
        if (Name.IsEmpty() || Record->GetStringField(TEXT("name")) == Name)
        {
            return Record;
        }
    }
    return nullptr;
}

TSharedPtr<FJsonObject> FindFamily(const TArray<TSharedPtr<FJsonValue>>& Matrix, const FString& Family)
{
    for (const TSharedPtr<FJsonValue>& Item : Matrix)
    {
        const TSharedPtr<FJsonObject> Record = Item.IsValid() ? Item->AsObject() : nullptr;
        if (Record.IsValid() && Record->GetStringField(TEXT("family")) == Family)
        {
            return Record;
        }
    }
    return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPBlueprintLibrariesInspectionTest,
    "UnrealMCP.BlueprintLibraries.InspectionOnlyFamilies",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPBlueprintLibrariesInspectionTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    using namespace UnrealMCP::BlueprintFamilyPolicy;

    const FString Base = TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    UBlueprint* FunctionLibrary = CreateLibraryFixture(
        Base + TEXT("/BFL_Inspection"), UBlueprintFunctionLibrary::StaticClass(),
        BPTYPE_FunctionLibrary, TEXT("ComputeValue"));
    UBlueprint* MacroLibrary = CreateLibraryFixture(
        Base + TEXT("/BML_Inspection"), AActor::StaticClass(),
        BPTYPE_MacroLibrary, TEXT("ExpandValue"));
    if (!TestNotNull(TEXT("function-library fixture creates"), FunctionLibrary)
        || !TestNotNull(TEXT("macro-library fixture creates"), MacroLibrary))
    {
        return false;
    }

    TestEqual(TEXT("function library classifies by Blueprint type"),
        ClassifyForInspection(FunctionLibrary).Name, FString(TEXT("function_library")));
    TestEqual(TEXT("Actor-scoped macro library classifies by Blueprint type"),
        ClassifyForInspection(MacroLibrary).Name, FString(TEXT("macro_library")));
    TestTrue(TEXT("function-library inspection is supported"),
        Supports(FunctionLibrary, EOperation::Inspect));
    TestTrue(TEXT("macro-library inspection is supported"),
        Supports(MacroLibrary, EOperation::Inspect));
    TestFalse(TEXT("function-library member mutation is excluded"),
        Supports(FunctionLibrary, EOperation::Members));
    TestFalse(TEXT("macro-library graph mutation is excluded"),
        Supports(MacroLibrary, EOperation::GraphEdit));

    const TArray<TSharedPtr<FJsonValue>> Matrix = BuildPublishedMatrix();
    TestEqual(TEXT("family matrix adds two bounded library families"), Matrix.Num(), 9);
    for (const TCHAR* FamilyName : {TEXT("function_library"), TEXT("macro_library")})
    {
        const TSharedPtr<FJsonObject> Family = FindFamily(Matrix, FamilyName);
        if (!TestTrue(*FString::Printf(TEXT("%s family is published"), FamilyName), Family.IsValid()))
        {
            return false;
        }
        const TSharedPtr<FJsonObject> Operations = Family->GetObjectField(TEXT("operations"));
        TestTrue(TEXT("library discovery is published"), Operations->GetBoolField(TEXT("discover")));
        TestTrue(TEXT("library inspection is published"), Operations->GetBoolField(TEXT("inspect")));
        for (const TCHAR* Operation : {TEXT("create"), TEXT("compile"), TEXT("save"),
            TEXT("class_defaults"), TEXT("components"), TEXT("widget_tree"), TEXT("member_variables"),
            TEXT("functions"), TEXT("local_variables"), TEXT("macros"), TEXT("custom_events"),
            TEXT("action_catalog"), TEXT("graph_edit"), TEXT("parent_change"),
            TEXT("project_settings_assignment")})
        {
            TestFalse(*FString::Printf(TEXT("%s excludes %s"), FamilyName, Operation),
                Operations->GetBoolField(Operation));
        }
    }

    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    struct FFixture
    {
        UBlueprint* Blueprint;
        const TCHAR* Family;
        const TCHAR* DeclarationSection;
        const TCHAR* DeclarationName;
        const TCHAR* GraphType;
    };
    for (const FFixture& Fixture : {
        FFixture{FunctionLibrary, TEXT("function_library"), TEXT("function"), TEXT("ComputeValue"), TEXT("function")},
        FFixture{MacroLibrary, TEXT("macro_library"), TEXT("macro"), TEXT("ExpandValue"), TEXT("macro")}})
    {
        const bool bDirtyBefore = Fixture.Blueprint->GetOutermost()->IsDirty();
        const EBlueprintStatus StatusBefore = Fixture.Blueprint->Status;
        const TSharedRef<FJsonObject> Discover = MakeShared<FJsonObject>();
        Discover->SetStringField(TEXT("mode"), TEXT("discover"));
        Discover->SetStringField(TEXT("package_path"), Base);
        Discover->SetStringField(TEXT("asset_name"), Fixture.Blueprint->GetName());
        if (!TestTrue(TEXT("library discovery succeeds"), Inspector.Execute(Discover, Result, Error)))
        {
            AddError(Error.Code + TEXT(": ") + Error.Message);
            return false;
        }
        const TSharedPtr<FJsonObject> AssetRecord = FindRecord(Result, TEXT("asset"));
        if (!TestTrue(TEXT("library discovery returns the exact asset"), AssetRecord.IsValid())) return false;
        TestEqual(TEXT("discovery reports the exact library family"),
            AssetRecord->GetStringField(TEXT("blueprint_family")), FString(Fixture.Family));

        const TSharedRef<FJsonObject> Inspect = AllSectionArguments(Fixture.Blueprint->GetPathName());
        Inspect->SetBoolField(TEXT("include_inherited"), true);
        TArray<TSharedPtr<FJsonValue>> Sections = Inspect->GetArrayField(TEXT("sections"));
        Sections.Add(MakeShared<FJsonValueString>(TEXT("class_defaults")));
        Inspect->SetArrayField(TEXT("sections"), Sections);
        Inspect->SetArrayField(
            TEXT("property_names"), {MakeShared<FJsonValueString>(TEXT("bReplicates"))});
        if (!TestTrue(TEXT("library graph inspection succeeds"),
            Inspector.Execute(Inspect, Result, Error)))
        {
            AddError(Error.Code + TEXT(": ") + Error.Message);
            return false;
        }
        TestEqual(TEXT("inspection reports the exact library family"),
            Result->GetStringField(TEXT("blueprint_family")), FString(Fixture.Family));
        const TSharedPtr<FJsonObject> Summary = FindRecord(Result, TEXT("summary"));
        const TSharedPtr<FJsonObject> Declaration = FindRecord(
            Result, Fixture.DeclarationSection, Fixture.DeclarationName);
        if (!TestTrue(TEXT("library inspection includes summary"), Summary.IsValid())
            || !TestTrue(TEXT("library inspection includes its declaration"), Declaration.IsValid())
            || !TestTrue(TEXT("library inspection includes graph structure"),
                FindRecord(Result, TEXT("graph"), Fixture.DeclarationName).IsValid()))
        {
            return false;
        }
        TestFalse(TEXT("library summary is not Actor authoring"),
            Summary->GetBoolField(TEXT("actor_blueprint")));
        TestEqual(TEXT("summary reports the exact library family"),
            Summary->GetStringField(TEXT("blueprint_family")), FString(Fixture.Family));
        TestFalse(TEXT("library declaration is inspection-only"),
            Declaration->GetBoolField(TEXT("editable")));
        const TSharedPtr<FJsonObject> Capabilities = Result->GetObjectField(TEXT("family_capabilities"));
        const TSharedPtr<FJsonObject> GraphTypes = Capabilities->GetObjectField(TEXT("graph_types"));
        TestTrue(TEXT("the owned library graph type is inspectable"),
            GraphTypes->GetBoolField(Fixture.GraphType));
        TestFalse(TEXT("library class defaults stay unavailable"),
            Capabilities->GetBoolField(TEXT("class_defaults")));
        TestFalse(TEXT("library components stay unavailable"),
            Capabilities->GetBoolField(TEXT("components")));
        TestFalse(TEXT("library inspection emits no inherited Actor components"),
            ResultHasSection(Result, TEXT("component")));
        TestFalse(TEXT("library inspection emits no class defaults"),
            ResultHasSection(Result, TEXT("class_default")));
        TestEqual(TEXT("inspection preserves package dirtiness"),
            Fixture.Blueprint->GetOutermost()->IsDirty(), bDirtyBefore);
        TestEqual(TEXT("inspection preserves compile status"), Fixture.Blueprint->Status, StatusBefore);

        const FString Snapshot = Result->GetStringField(TEXT("snapshot_id"));
        TestFalse(TEXT("library compilation remains excluded"), Mutator.Execute(
            TEXT("blueprint_compile"), AssetArguments(Fixture.Blueprint->GetPathName()), Result, Error));
        TestEqual(TEXT("library mutation rejection is stable"), Error.Code, FString(TEXT("wrong_type")));
        TestEqual(TEXT("rejected mutation preserves the snapshot"),
            InspectSnapshot(Inspector, Fixture.Blueprint->GetPathName()), Snapshot);
    }
    return true;
}

#endif
