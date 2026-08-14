#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPBlueprintAutomationTestSupport.h"
#include "UnrealMCPGameplayTagTestTypes.h"

#include "GameplayTagsManager.h"
#include "GameplayTagsSettings.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Misc/ScopeExit.h"

namespace
{
TSharedPtr<FUnrealMCPValue> TagArray(std::initializer_list<const TCHAR*> Names)
{
    TArray<TSharedPtr<FUnrealMCPValue>> Values;
    for (const TCHAR* Name : Names)
    {
        Values.Add(MakeShared<FUnrealMCPValueString>(Name));
    }
    return MakeShared<FUnrealMCPValueArray>(Values);
}

void SetLegacyTagName(FGameplayTag& Tag, const TCHAR* Name)
{
    FNameProperty* TagName = FindFProperty<FNameProperty>(FGameplayTag::StaticStruct(), TEXT("TagName"));
    check(TagName != nullptr);
    TagName->SetPropertyValue(TagName->ContainerPtrToValuePtr<void>(&Tag), FName(Name));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPGameplayTagPropertyCodecTest,
    "UnrealMCP.GameplayTagProperties.CodecValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPGameplayTagPropertyCodecTest::RunTest(const FString& Parameters)
{
    AUnrealMCPGameplayTagTestActor* Fixture = NewObject<AUnrealMCPGameplayTagTestActor>();
    TSharedPtr<FUnrealMCPRecord> Changed;
    FUnrealMCPError Error;

    TestTrue(TEXT("registered tag writes"), UnrealMCP::PropertyCodec::Set(
        Fixture, TEXT("SingleTag"), MakeShared<FUnrealMCPValueString>(TEXT("UnrealMCP.Test.Child")), Changed, Error));
    TestEqual(TEXT("tag type is semantic"), Changed->GetStringField(TEXT("type")), FString(TEXT("gameplay_tag")));
    TestEqual(TEXT("tag reads back exactly"), Changed->GetStringField(TEXT("value")), FString(TEXT("UnrealMCP.Test.Child")));
    TestTrue(TEXT("empty tag writes"), UnrealMCP::PropertyCodec::Set(
        Fixture, TEXT("SingleTag"), MakeShared<FUnrealMCPValueString>(FString()), Changed, Error));
    TestEqual(TEXT("empty tag reads back as empty string"), Changed->GetStringField(TEXT("value")), FString());

    TestTrue(TEXT("child-only container writes"), UnrealMCP::PropertyCodec::Set(
        Fixture, TEXT("TagContainer"), TagArray({TEXT("UnrealMCP.Test.Child")}), Changed, Error));
    TestEqual(TEXT("derived parent is omitted"), Changed->GetArrayField(TEXT("value")).Num(), 1);
    TestTrue(TEXT("explicit parent and child write"), UnrealMCP::PropertyCodec::Set(
        Fixture, TEXT("TagContainer"),
        TagArray({TEXT("UnrealMCP.Test.Child"), TEXT("UnrealMCP.Test")}), Changed, Error));
    const TArray<TSharedPtr<FUnrealMCPValue>>& Ordered = Changed->GetArrayField(TEXT("value"));
    TestEqual(TEXT("container reads back both explicit tags"), Ordered.Num(), 2);
    TestEqual(TEXT("container order is deterministic"), Ordered[0]->AsString(), FString(TEXT("UnrealMCP.Test")));
    TestEqual(TEXT("container keeps exact child"), Ordered[1]->AsString(), FString(TEXT("UnrealMCP.Test.Child")));

    const TSharedRef<FUnrealMCPRecord> BeforeRejections = UnrealMCP::PropertyCodec::Encode(
        Fixture, FindFProperty<FProperty>(Fixture->GetClass(), TEXT("TagContainer")));
    TestFalse(TEXT("duplicate tags reject"), UnrealMCP::PropertyCodec::Set(
        Fixture, TEXT("TagContainer"),
        TagArray({TEXT("UnrealMCP.Test.Child"), TEXT("UnrealMCP.Test.Child")}), Changed, Error));
    TestEqual(TEXT("duplicate rejection is stable"), Error.Code, FString(TEXT("invalid_argument")));
    TestFalse(TEXT("unknown tag rejects"), UnrealMCP::PropertyCodec::Set(
        Fixture, TEXT("SingleTag"), MakeShared<FUnrealMCPValueString>(TEXT("UnrealMCP.Test.Unknown")), Changed, Error));
    TestFalse(TEXT("malformed tag rejects"), UnrealMCP::PropertyCodec::Set(
        Fixture, TEXT("SingleTag"), MakeShared<FUnrealMCPValueString>(TEXT("UnrealMCP..Bad")), Changed, Error));
    TestFalse(TEXT("overlong tag rejects"), UnrealMCP::PropertyCodec::Set(
        Fixture, TEXT("SingleTag"),
        MakeShared<FUnrealMCPValueString>(FString::ChrN(UnrealMCP::MaxGameplayTagChars + 1, TEXT('A'))),
        Changed, Error));
    TestEqual(TEXT("overlong rejection is bounded"), Error.Code, FString(TEXT("data_limit_exceeded")));
    TArray<TSharedPtr<FUnrealMCPValue>> TooMany;
    TooMany.Init(MakeShared<FUnrealMCPValueString>(TEXT("UnrealMCP.Test.Child")),
        UnrealMCP::MaxGameplayTagsPerContainer + 1);
    TestFalse(TEXT("over-limit container rejects"), UnrealMCP::PropertyCodec::Set(
        Fixture, TEXT("TagContainer"), MakeShared<FUnrealMCPValueArray>(TooMany), Changed, Error));
    TestEqual(TEXT("container rejection preserves prior value"),
        UnrealMCP::PropertyCodec::Encode(Fixture, FindFProperty<FProperty>(Fixture->GetClass(), TEXT("TagContainer")))
            ->GetArrayField(TEXT("value")).Num(),
        BeforeRejections->GetArrayField(TEXT("value")).Num());

    UGameplayTagsSettings* Settings = GetMutableDefault<UGameplayTagsSettings>();
    const TArray<FGameplayTagRedirect> OriginalRedirects = Settings->GameplayTagRedirects;
    ON_SCOPE_EXIT
    {
        Settings->GameplayTagRedirects = OriginalRedirects;
        UGameplayTagsManager::Get().EditorRefreshGameplayTagTree();
    };
    FGameplayTagRedirect Redirect;
    Redirect.OldTagName = TEXT("UnrealMCP.Test.Redirected");
    Redirect.NewTagName = TEXT("UnrealMCP.Test.Child");
    Settings->GameplayTagRedirects.Add(Redirect);
    UGameplayTagsManager::Get().EditorRefreshGameplayTagTree();
    TestFalse(TEXT("redirected input rejects instead of canonicalizing"), UnrealMCP::PropertyCodec::Set(
        Fixture, TEXT("SingleTag"), MakeShared<FUnrealMCPValueString>(TEXT("UnrealMCP.Test.Redirected")), Changed, Error));

    SetLegacyTagName(Fixture->SingleTag, TEXT("Legacy.Invalid?"));
    const TSharedRef<FUnrealMCPRecord> Legacy = UnrealMCP::PropertyCodec::Encode(
        Fixture, FindFProperty<FProperty>(Fixture->GetClass(), TEXT("SingleTag")));
    TestTrue(TEXT("legacy invalid stored tag remains inspectable"), Legacy->GetBoolField(TEXT("supported")));
    TestEqual(TEXT("legacy invalid stored name remains exact"),
        Legacy->GetStringField(TEXT("value")), FString(TEXT("Legacy.Invalid?")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPGameplayTagBlueprintWorkflowTest,
    "UnrealMCP.GameplayTagProperties.BlueprintDefaultsAndComponents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPGameplayTagBlueprintWorkflowTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString PackageName = TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_GameplayTags");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AUnrealMCPGameplayTagTestActor::StaticClass(), false);
    if (!TestNotNull(TEXT("Gameplay Tag Blueprint fixture exists"), Blueprint)) return false;
    USCS_Node* TagNode = Blueprint->SimpleConstructionScript->CreateNode(
        UUnrealMCPGameplayTagTestComponent::StaticClass(), TEXT("TagComponent"));
    Blueprint->SimpleConstructionScript->AddNode(TagNode);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    FString Snapshot = InspectSnapshot(Inspector, Blueprint->GetPathName());

    const TSharedRef<FUnrealMCPRecord> SetDefault = MakeShared<FUnrealMCPRecord>();
    SetDefault->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    SetDefault->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
    SetDefault->SetStringField(TEXT("expected_snapshot"), Snapshot);
    SetDefault->SetStringField(TEXT("property_name"), TEXT("SingleTag"));
    SetDefault->SetStringField(TEXT("value"), TEXT("UnrealMCP.Test.Child"));
    if (!TestTrue(TEXT("Blueprint class-default tag writes"),
        Mutator.Execute(TEXT("blueprint_default_edit"), SetDefault, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestEqual(TEXT("class-default tag reads back"),
        Result->GetObjectField(TEXT("changed"))->GetStringField(TEXT("value")),
        FString(TEXT("UnrealMCP.Test.Child")));
    const FString DefaultSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    TestTrue(TEXT("class-default tag edit undoes"), GEditor != nullptr && GEditor->UndoTransaction());
    TestEqual(TEXT("tag Undo restores snapshot"), InspectSnapshot(Inspector, Blueprint->GetPathName()), Snapshot);
    TestTrue(TEXT("class-default tag edit redoes"), GEditor != nullptr && GEditor->RedoTransaction());
    TestEqual(TEXT("tag Redo restores snapshot"), InspectSnapshot(Inspector, Blueprint->GetPathName()), DefaultSnapshot);

    const FString ComponentId = ComponentIdByName(Inspector, Blueprint->GetPathName(), TEXT("TagComponent"));
    TSharedRef<FUnrealMCPRecord> SetComponent = ComponentEditArguments(
        Blueprint->GetPathName(), DefaultSnapshot, TEXT("set_property"));
    SetComponent->SetStringField(TEXT("component_id"), ComponentId);
    SetComponent->SetStringField(TEXT("property_name"), TEXT("TagContainer"));
    SetComponent->SetField(TEXT("value"), TagArray({TEXT("UnrealMCP.Test.Child"), TEXT("UnrealMCP.Test")}));
    if (!TestTrue(TEXT("component-default tag container writes"),
        Mutator.Execute(TEXT("blueprint_component_edit"), SetComponent, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestEqual(TEXT("component tag container reads back"),
        Result->GetObjectField(TEXT("changed"))->GetArrayField(TEXT("value")).Num(), 2);
    const FString ComponentSnapshot = Result->GetStringField(TEXT("snapshot_id"));
    SetComponent->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    SetComponent->SetStringField(TEXT("expected_snapshot"), DefaultSnapshot);
    TestFalse(TEXT("stale tag component edit rejects"),
        Mutator.Execute(TEXT("blueprint_component_edit"), SetComponent, Result, Error));
    TestEqual(TEXT("stale tag rejection preserves snapshot"),
        InspectSnapshot(Inspector, Blueprint->GetPathName()), ComponentSnapshot);

    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
    TestEqual(TEXT("Gameplay Tag Blueprint compiles"), Log.NumErrors, 0);
    TestTrue(TEXT("Gameplay Tag Blueprint saves"), SaveBlueprintFixture(Blueprint));
    return true;
}

#endif
