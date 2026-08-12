#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "K2Node_VariableGet.h"
#include "UnrealMCPWidgetTreeService.h"
#include "WidgetBlueprint.h"

namespace
{
TSharedRef<FUnrealMCPRecord> WidgetEdit(
    const FString& AssetPath,
    const FString& Snapshot,
    const FString& Operation)
{
    const TSharedRef<FUnrealMCPRecord> Arguments = MakeShared<FUnrealMCPRecord>();
    Arguments->SetStringField(
        TEXT("operation_id"),
        FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    Arguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
    Arguments->SetStringField(TEXT("operation"), Operation);
    return Arguments;
}

TSharedRef<FUnrealMCPRecord> PanelTarget(const FString& ParentId)
{
    const TSharedRef<FUnrealMCPRecord> Target = MakeShared<FUnrealMCPRecord>();
    Target->SetStringField(TEXT("kind"), TEXT("panel"));
    Target->SetStringField(TEXT("parent_id"), ParentId);
    return Target;
}

FString RecordId(
    FUnrealMCPBlueprintInspector& Inspector,
    const FString& AssetPath,
    const FString& Section,
    const FString& Name)
{
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    const TSharedRef<FUnrealMCPRecord> Arguments =
        UnrealMCP::Tests::InspectArguments(AssetPath);
    Arguments->SetArrayField(
        TEXT("sections"),
        {MakeShared<FUnrealMCPValueString>(TEXT("widget_tree"))});
    if (!Inspector.Execute(Arguments, Result, Error))
    {
        return FString();
    }
    for (const TSharedPtr<FUnrealMCPValue>& Item : Result->GetArrayField(TEXT("records")))
    {
        const TSharedPtr<FUnrealMCPRecord> Record = Item->AsObject();
        if (Record.IsValid()
            && Record->GetStringField(TEXT("section")) == Section
            && Record->GetStringField(TEXT("name")) == Name)
        {
            return Record->GetStringField(TEXT("id"));
        }
    }
    return FString();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPWidgetTreeTest,
    "UnrealMCP.WidgetTree.FamilyInspectionMutationAndPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPWidgetTreeTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    using namespace UnrealMCP::BlueprintFamilyPolicy;

    TestEqual(
        TEXT("UUserWidget classifies as the widget family"),
        Classify(UUserWidget::StaticClass()).Name,
        FString(TEXT("widget")));
    TestTrue(
        TEXT("Widget family supports tree editing"),
        Supports(UUserWidget::StaticClass(), EOperation::WidgetTree));
    TestFalse(
        TEXT("Widget family rejects Actor component editing"),
        Supports(UUserWidget::StaticClass(), EOperation::Components));

    TSharedPtr<FUnrealMCPRecord> WidgetFamily;
    for (const TSharedPtr<FUnrealMCPValue>& Value : BuildPublishedMatrix())
    {
        const TSharedPtr<FUnrealMCPRecord> Record = Value->AsObject();
        if (Record.IsValid()
            && Record->GetStringField(TEXT("family")) == TEXT("widget"))
        {
            WidgetFamily = Record;
            break;
        }
    }
    if (!TestTrue(TEXT("Widget family is published"), WidgetFamily.IsValid()))
    {
        return false;
    }
    TestEqual(
        TEXT("Widget family has the exact inheritance category"),
        WidgetFamily->GetStringField(TEXT("inheritance_category")),
        FString(TEXT("widget_derived")));
    TestTrue(
        TEXT("Widget tree operation is published"),
        WidgetFamily->GetObjectField(TEXT("operations"))
            ->GetBoolField(TEXT("widget_tree")));
    TestFalse(
        TEXT("Actor components stay unavailable"),
        WidgetFamily->GetObjectField(TEXT("operations"))
            ->GetBoolField(TEXT("components")));

    const FString PackageName = TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits)
        + TEXT("/WBP_WidgetTree");
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    FUnrealMCPWidgetTreeService Widgets(Inspector);
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    if (!TestTrue(
        TEXT("Widget Blueprint creation succeeds"),
        Mutator.Execute(
            TEXT("blueprint_create"),
            CreateArguments(UUserWidget::StaticClass()->GetPathName(), PackageName),
            Result,
            Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const FString AssetPath = Result->GetStringField(TEXT("asset_path"));
    UWidgetBlueprint* Blueprint =
        LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
    if (!TestNotNull(TEXT("creation produces UWidgetBlueprint"), Blueprint))
    {
        return false;
    }
    TestEqual(
        TEXT("creation reports widget family"),
        Result->GetStringField(TEXT("blueprint_family")),
        FString(TEXT("widget")));
    const TSharedPtr<FUnrealMCPRecord> Live =
        Result->GetObjectField(TEXT("family_capabilities"));
    TestTrue(
        TEXT("live widget-tree capability is available"),
        Live->GetBoolField(TEXT("widget_tree")));
    TestFalse(
        TEXT("live component capability is unavailable"),
        Live->GetBoolField(TEXT("components")));

    FString Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FUnrealMCPRecord> Root =
        WidgetEdit(AssetPath, Snapshot, TEXT("set_root"));
    Root->SetStringField(
        TEXT("widget_class"),
        UCanvasPanel::StaticClass()->GetPathName());
    Root->SetStringField(TEXT("name"), TEXT("RootCanvas"));
    if (!TestTrue(
        TEXT("root panel creation succeeds"),
        Widgets.Execute(Root, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const FString RootId = Result->GetStringField(TEXT("widget_id"));
    TestEqual(TEXT("root identity is stable"), RootId.Len(), 32);
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    TSharedRef<FUnrealMCPRecord> DuplicateRoot =
        WidgetEdit(AssetPath, Snapshot, TEXT("set_root"));
    DuplicateRoot->SetStringField(
        TEXT("widget_class"),
        UCanvasPanel::StaticClass()->GetPathName());
    DuplicateRoot->SetStringField(TEXT("name"), TEXT("OtherRoot"));
    TestFalse(
        TEXT("a second root rejects"),
        Widgets.Execute(DuplicateRoot, Result, Error));
    TestEqual(
        TEXT("root rejection is stable"),
        Error.Code,
        FString(TEXT("write_conflict")));
    TestEqual(
        TEXT("root rejection preserves snapshot"),
        InspectSnapshot(Inspector, AssetPath),
        Snapshot);

    TSharedRef<FUnrealMCPRecord> AddText =
        WidgetEdit(AssetPath, Snapshot, TEXT("add"));
    AddText->SetStringField(
        TEXT("widget_class"),
        UTextBlock::StaticClass()->GetPathName());
    AddText->SetStringField(TEXT("name"), TEXT("StatusText"));
    AddText->SetObjectField(TEXT("target"), PanelTarget(RootId));
    if (!TestTrue(
        TEXT("text child creation succeeds"),
        Widgets.Execute(AddText, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const FString TextId = Result->GetStringField(TEXT("widget_id"));
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    TSharedRef<FUnrealMCPRecord> Rename =
        WidgetEdit(AssetPath, Snapshot, TEXT("rename"));
    Rename->SetStringField(TEXT("widget_id"), TextId);
    Rename->SetStringField(TEXT("new_name"), TEXT("HUDStatus"));
    if (!TestTrue(
        TEXT("widget rename succeeds"),
        Widgets.Execute(Rename, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestEqual(
        TEXT("rename preserves the widget identity"),
        Result->GetStringField(TEXT("widget_id")),
        TextId);
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    TSharedRef<FUnrealMCPRecord> SetVariable =
        WidgetEdit(AssetPath, Snapshot, TEXT("set_variable"));
    SetVariable->SetStringField(TEXT("widget_id"), TextId);
    SetVariable->SetBoolField(TEXT("is_variable"), true);
    if (!TestTrue(
        TEXT("variable exposure succeeds"),
        Widgets.Execute(SetVariable, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    TSharedRef<FUnrealMCPRecord> SetOpacity =
        WidgetEdit(AssetPath, Snapshot, TEXT("set_property"));
    SetOpacity->SetStringField(TEXT("widget_id"), TextId);
    SetOpacity->SetStringField(TEXT("property_name"), TEXT("RenderOpacity"));
    SetOpacity->SetNumberField(TEXT("value"), 0.75);
    if (!TestTrue(
        TEXT("safe widget default edit succeeds"),
        Widgets.Execute(SetOpacity, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    const TSharedRef<FUnrealMCPRecord> InspectDefaults =
        InspectArguments(AssetPath);
    InspectDefaults->SetArrayField(
        TEXT("sections"),
        {
            MakeShared<FUnrealMCPValueString>(TEXT("widget_tree")),
            MakeShared<FUnrealMCPValueString>(TEXT("widget_defaults")),
        });
    InspectDefaults->SetStringField(TEXT("widget_id"), TextId);
    InspectDefaults->SetArrayField(
        TEXT("property_names"),
        {MakeShared<FUnrealMCPValueString>(TEXT("RenderOpacity"))});
    if (!TestTrue(
        TEXT("targeted widget-default inspection succeeds"),
        Inspector.Execute(InspectDefaults, Result, Error)))
    {
        return false;
    }
    bool bFoundOpacity = false;
    for (const TSharedPtr<FUnrealMCPValue>& Item : Result->GetArrayField(TEXT("records")))
    {
        const TSharedPtr<FUnrealMCPRecord> Record = Item->AsObject();
        if (Record.IsValid()
            && Record->GetStringField(TEXT("section")) == TEXT("widget_default")
            && Record->GetStringField(TEXT("name")) == TEXT("RenderOpacity")
            && FMath::IsNearlyEqual(Record->GetNumberField(TEXT("value")), 0.75))
        {
            bFoundOpacity = true;
        }
    }
    TestTrue(TEXT("edited widget default reads back"), bFoundOpacity);

    TSharedRef<FUnrealMCPRecord> AddBorder =
        WidgetEdit(AssetPath, Snapshot, TEXT("add"));
    AddBorder->SetStringField(
        TEXT("widget_class"),
        UBorder::StaticClass()->GetPathName());
    AddBorder->SetStringField(TEXT("name"), TEXT("StatusPanel"));
    AddBorder->SetObjectField(TEXT("target"), PanelTarget(RootId));
    if (!TestTrue(
        TEXT("single-child panel creation succeeds"),
        Widgets.Execute(AddBorder, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const FString BorderId = Result->GetStringField(TEXT("widget_id"));
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    TSharedRef<FUnrealMCPRecord> Reparent =
        WidgetEdit(AssetPath, Snapshot, TEXT("reparent"));
    Reparent->SetStringField(TEXT("widget_id"), TextId);
    Reparent->SetObjectField(TEXT("target"), PanelTarget(BorderId));
    if (!TestTrue(
        TEXT("widget reparent succeeds"),
        Widgets.Execute(Reparent, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const FString ReparentedSnapshot =
        Result->GetStringField(TEXT("snapshot_id"));
    UWidget* TextWidget = Blueprint->WidgetTree->FindWidget(TEXT("HUDStatus"));
    UWidget* BorderWidget = Blueprint->WidgetTree->FindWidget(TEXT("StatusPanel"));
    TestEqual(
        TEXT("reparent changes the live hierarchy"),
        TextWidget != nullptr ? TextWidget->GetParent() : nullptr,
        Cast<UPanelWidget>(BorderWidget));
    TestTrue(
        TEXT("reparent transaction undoes"),
        GEditor != nullptr && GEditor->UndoTransaction());
    TestEqual(
        TEXT("undo restores root parent"),
        TextWidget != nullptr ? TextWidget->GetParent() : nullptr,
        Cast<UPanelWidget>(Blueprint->WidgetTree->RootWidget));
    TestTrue(
        TEXT("reparent transaction redoes"),
        GEditor != nullptr && GEditor->RedoTransaction());
    TestEqual(
        TEXT("redo restores panel parent"),
        TextWidget != nullptr ? TextWidget->GetParent() : nullptr,
        Cast<UPanelWidget>(BorderWidget));
    TestEqual(
        TEXT("redo restores structural snapshot"),
        InspectSnapshot(Inspector, AssetPath),
        ReparentedSnapshot);
    Snapshot = ReparentedSnapshot;

    TSharedRef<FUnrealMCPRecord> StaleRename =
        WidgetEdit(AssetPath, Snapshot, TEXT("rename"));
    StaleRename->SetStringField(
        TEXT("expected_snapshot"),
        FString::ChrN(40, TEXT('0')));
    StaleRename->SetStringField(TEXT("widget_id"), TextId);
    StaleRename->SetStringField(TEXT("new_name"), TEXT("StaleName"));
    TestFalse(
        TEXT("stale mutation rejects"),
        Widgets.Execute(StaleRename, Result, Error));
    TestEqual(
        TEXT("stale mutation error is stable"),
        Error.Code,
        FString(TEXT("stale_precondition")));

    TSharedRef<FUnrealMCPRecord> Component = ComponentEditArguments(
        AssetPath, Snapshot, TEXT("add"));
    Component->SetStringField(
        TEXT("component_class"),
        USceneComponent::StaticClass()->GetPathName());
    Component->SetStringField(TEXT("name"), TEXT("InvalidComponent"));
    TestFalse(
        TEXT("Widget Blueprint rejects Actor components"),
        Mutator.Execute(
            TEXT("blueprint_component_edit"), Component, Result, Error));
    TestEqual(
        TEXT("component rejection is explicit"),
        Error.Code,
        FString(TEXT("invalid_component")));

    TSharedRef<FUnrealMCPRecord> Compile = AssetArguments(AssetPath);
    Compile->SetStringField(
        TEXT("operation_id"),
        FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Compile->SetStringField(TEXT("expected_snapshot"), Snapshot);
    if (!TestTrue(
        TEXT("Widget Blueprint compiles"),
        Mutator.Execute(TEXT("blueprint_compile"), Compile, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestTrue(
        TEXT("Widget Blueprint compile succeeds"),
        Result->GetBoolField(TEXT("compile_succeeded")));
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    UEdGraph* EventGraph =
        !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("Widget Blueprint event graph exists"), EventGraph))
    {
        return false;
    }
    EventGraph->Modify();
    FProperty* WidgetProperty = Blueprint->SkeletonGeneratedClass != nullptr
        ? Blueprint->SkeletonGeneratedClass->FindPropertyByName(TEXT("HUDStatus"))
        : nullptr;
    if (!TestNotNull(TEXT("exposed widget property exists"), WidgetProperty))
    {
        return false;
    }
    UK2Node_VariableGet* Getter = NewObject<UK2Node_VariableGet>(EventGraph);
    Getter->SetFromProperty(
        WidgetProperty, true, Blueprint->SkeletonGeneratedClass);
    Getter->CreateNewGuid();
    EventGraph->AddNode(Getter, true, false);
    Getter->PostPlacedNewNode();
    Getter->AllocateDefaultPins();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Snapshot = InspectSnapshot(Inspector, AssetPath);
    FDelegateEditorBinding WidgetBinding;
    WidgetBinding.ObjectName = TEXT("HUDStatus");
    Blueprint->Bindings.Add(WidgetBinding);
    Snapshot = InspectSnapshot(Inspector, AssetPath);

    TSharedRef<FUnrealMCPRecord> RemoveReferenced =
        WidgetEdit(AssetPath, Snapshot, TEXT("remove"));
    RemoveReferenced->SetStringField(TEXT("widget_id"), TextId);
    RemoveReferenced->SetStringField(
        TEXT("policy"), TEXT("reject_if_referenced"));
    TestFalse(
        TEXT("referenced widget removal rejects"),
        Widgets.Execute(RemoveReferenced, Result, Error));
    TestEqual(
        TEXT("referenced widget error is stable"),
        Error.Code,
        FString(TEXT("referenced")));
    TestEqual(
        TEXT("reference rejection preserves snapshot"),
        InspectSnapshot(Inspector, AssetPath),
        Snapshot);

    EventGraph->RemoveNode(Getter);
    Blueprint->Bindings.Reset();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FUnrealMCPRecord> Remove =
        WidgetEdit(AssetPath, Snapshot, TEXT("remove"));
    Remove->SetStringField(TEXT("widget_id"), TextId);
    Remove->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    if (!TestTrue(
        TEXT("unreferenced non-root widget removal succeeds"),
        Widgets.Execute(Remove, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    TSharedRef<FUnrealMCPRecord> Save = AssetArguments(AssetPath);
    Save->SetStringField(
        TEXT("operation_id"),
        FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Save->SetStringField(TEXT("expected_snapshot"), Snapshot);
    if (!TestTrue(
        TEXT("Widget Blueprint saves"),
        Mutator.Execute(TEXT("blueprint_save"), Save, Result, Error)))
    {
        return false;
    }
    TestFalse(
        TEXT("saved Widget Blueprint is clean"),
        Result->GetBoolField(TEXT("package_dirty")));
    TestEqual(
        TEXT("root identity reads back after save"),
        RecordId(Inspector, AssetPath, TEXT("widget"), TEXT("RootCanvas")),
        RootId);
    TestEqual(
        TEXT("removed widget stays absent"),
        RecordId(Inspector, AssetPath, TEXT("widget"), TEXT("HUDStatus")),
        FString());
    return true;
}

#endif
