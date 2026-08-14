#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"
#include "UnrealMCPBlueprintMutator.h"

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/ComboBoxString.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "UnrealMCPWidgetTreeService.h"
#include "WidgetBlueprint.h"

namespace
{
TSharedRef<FUnrealMCPRecord> UMGEdit(
    const FString& AssetPath,
    const FString& Snapshot,
    const FString& Operation)
{
    TSharedRef<FUnrealMCPRecord> Arguments =
        UnrealMCP::Tests::AssetArguments(AssetPath);
    Arguments->SetStringField(
        TEXT("operation_id"),
        FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
    Arguments->SetStringField(TEXT("operation"), Operation);
    return Arguments;
}

TSharedRef<FUnrealMCPRecord> UMGPanelTarget(const FString& ParentId)
{
    const TSharedRef<FUnrealMCPRecord> Target = MakeShared<FUnrealMCPRecord>();
    Target->SetStringField(TEXT("kind"), TEXT("panel"));
    Target->SetStringField(TEXT("parent_id"), ParentId);
    return Target;
}

TSharedRef<FUnrealMCPRecord> StructValue(
    const TSharedRef<FUnrealMCPRecord>& Fields)
{
    const TSharedRef<FUnrealMCPRecord> Value = MakeShared<FUnrealMCPRecord>();
    Value->SetStringField(TEXT("kind"), TEXT("struct"));
    Value->SetObjectField(TEXT("fields"), Fields);
    return Value;
}

FString SlotIdFor(
    FUnrealMCPBlueprintInspector& Inspector,
    const FString& AssetPath,
    const FString& WidgetId)
{
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    const TSharedRef<FUnrealMCPRecord> Arguments =
        UnrealMCP::Tests::InspectArguments(AssetPath);
    Arguments->SetArrayField(
        TEXT("sections"),
        {MakeShared<FUnrealMCPValueString>(TEXT("widget_tree"))});
    Arguments->SetStringField(TEXT("widget_id"), WidgetId);
    if (!Inspector.Execute(Arguments, Result, Error))
    {
        return FString();
    }
    for (const TSharedPtr<FUnrealMCPValue>& Item :
        Result->GetArrayField(TEXT("records")))
    {
        const TSharedPtr<FUnrealMCPRecord> Record = Item->AsObject();
        if (Record.IsValid()
            && Record->GetStringField(TEXT("section")) == TEXT("widget")
            && Record->GetStringField(TEXT("id")) == WidgetId)
        {
            return Record->GetStringField(TEXT("slot_id"));
        }
    }
    return FString();
}

bool HasBindingRecord(
    const TSharedPtr<FUnrealMCPRecord>& Result,
    const FString& WidgetId,
    const FString& RecordType)
{
    if (!Result.IsValid())
    {
        return false;
    }
    for (const TSharedPtr<FUnrealMCPValue>& Item :
        Result->GetArrayField(TEXT("records")))
    {
        const TSharedPtr<FUnrealMCPRecord> Record = Item->AsObject();
        if (Record.IsValid()
            && Record->GetStringField(TEXT("section"))
                == TEXT("widget_bindings")
            && Record->GetStringField(TEXT("widget_id")) == WidgetId
            && Record->GetStringField(TEXT("record_type")) == RecordType)
        {
            return true;
        }
    }
    return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPUMGAuthoringTest,
    "UnrealMCP.UMGAuthoring.LayoutStyleBindingsAndEvents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPUMGAuthoringTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;

    const FString PackageName = TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits)
        + TEXT("/WBP_UMGAuthoring");
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    FUnrealMCPWidgetTreeService Widgets(Inspector);
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    if (!TestTrue(
            TEXT("UMG fixture creation succeeds"),
            Mutator.Execute(
                TEXT("blueprint_create"),
                CreateArguments(
                    UUserWidget::StaticClass()->GetPathName(), PackageName),
                Result,
                Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const FString AssetPath = Result->GetStringField(TEXT("asset_path"));
    UWidgetBlueprint* Blueprint =
        LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
    if (!TestNotNull(TEXT("UMG fixture is a Widget Blueprint"), Blueprint))
    {
        return false;
    }

    FEdGraphPinType FloatType;
    FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
    FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    TestTrue(
        TEXT("resource-value source member is created"),
        FBlueprintEditorUtils::AddMemberVariable(
            Blueprint, TEXT("ResourceValue"), FloatType, TEXT("0.5")));
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    FString Snapshot = InspectSnapshot(Inspector, AssetPath);

    TSharedRef<FUnrealMCPRecord> Root =
        UMGEdit(AssetPath, Snapshot, TEXT("set_root"));
    Root->SetStringField(
        TEXT("widget_class"), UCanvasPanel::StaticClass()->GetPathName());
    Root->SetStringField(TEXT("name"), TEXT("RootCanvas"));
    if (!TestTrue(
            TEXT("responsive root is created"),
            Widgets.Execute(Root, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    const FString RootId = Result->GetStringField(TEXT("widget_id"));
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    auto AddWidget = [&](
        UClass* Class,
        const FString& Name,
        FString& OutId) -> bool
    {
        TSharedRef<FUnrealMCPRecord> Add =
            UMGEdit(AssetPath, Snapshot, TEXT("add"));
        Add->SetStringField(TEXT("widget_class"), Class->GetPathName());
        Add->SetStringField(TEXT("name"), Name);
        Add->SetObjectField(TEXT("target"), UMGPanelTarget(RootId));
        if (!Widgets.Execute(Add, Result, Error))
        {
            AddError(Error.Code + TEXT(": ") + Error.Message);
            return false;
        }
        OutId = Result->GetStringField(TEXT("widget_id"));
        Snapshot = Result->GetStringField(TEXT("snapshot_id"));
        return true;
    };
    FString TextId;
    FString ProgressId;
    FString ButtonId;
    FString ComboId;
    if (!TestTrue(
            TEXT("status text is added"),
            AddWidget(UTextBlock::StaticClass(), TEXT("StatusText"), TextId))
        || !TestTrue(
            TEXT("resource bar is added"),
            AddWidget(
                UProgressBar::StaticClass(), TEXT("ResourceBar"), ProgressId))
        || !TestTrue(
            TEXT("pause button is added"),
            AddWidget(UButton::StaticClass(), TEXT("PauseButton"), ButtonId))
        || !TestTrue(
            TEXT("settings selector is added"),
            AddWidget(
                UComboBoxString::StaticClass(),
                TEXT("QualitySelector"),
                ComboId)))
    {
        return false;
    }

    const FString TextSlotId = SlotIdFor(Inspector, AssetPath, TextId);
    TestEqual(TEXT("canvas slot identity is stable"), TextSlotId.Len(), 32);
    const TSharedRef<FUnrealMCPRecord> Anchors = MakeShared<FUnrealMCPRecord>();
    const TSharedRef<FUnrealMCPRecord> Minimum = MakeShared<FUnrealMCPRecord>();
    Minimum->SetNumberField(TEXT("X"), 0.0);
    Minimum->SetNumberField(TEXT("Y"), 0.0);
    const TSharedRef<FUnrealMCPRecord> Maximum = MakeShared<FUnrealMCPRecord>();
    Maximum->SetNumberField(TEXT("X"), 1.0);
    Maximum->SetNumberField(TEXT("Y"), 0.0);
    Anchors->SetObjectField(TEXT("Minimum"), StructValue(Minimum));
    Anchors->SetObjectField(TEXT("Maximum"), StructValue(Maximum));
    const TSharedRef<FUnrealMCPRecord> Offsets = MakeShared<FUnrealMCPRecord>();
    Offsets->SetNumberField(TEXT("Left"), 24.0);
    Offsets->SetNumberField(TEXT("Top"), 24.0);
    Offsets->SetNumberField(TEXT("Right"), -24.0);
    Offsets->SetNumberField(TEXT("Bottom"), 64.0);
    const TSharedRef<FUnrealMCPRecord> LayoutFields = MakeShared<FUnrealMCPRecord>();
    LayoutFields->SetObjectField(TEXT("Anchors"), StructValue(Anchors));
    LayoutFields->SetObjectField(TEXT("Offsets"), StructValue(Offsets));
    TSharedRef<FUnrealMCPRecord> SetLayout =
        UMGEdit(AssetPath, Snapshot, TEXT("set_slot"));
    SetLayout->SetStringField(TEXT("slot_id"), TextSlotId);
    SetLayout->SetStringField(TEXT("property_name"), TEXT("LayoutData"));
    SetLayout->SetObjectField(TEXT("value"), StructValue(LayoutFields));
    if (!TestTrue(
            TEXT("typed responsive Canvas layout succeeds"),
            Widgets.Execute(SetLayout, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    TSharedRef<FUnrealMCPRecord> SetText =
        UMGEdit(AssetPath, Snapshot, TEXT("set_style"));
    SetText->SetStringField(TEXT("widget_id"), TextId);
    SetText->SetStringField(TEXT("property_name"), TEXT("Text"));
    SetText->SetStringField(TEXT("value"), TEXT("Resources"));
    if (!TestTrue(
            TEXT("text presentation succeeds"),
            Widgets.Execute(SetText, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FUnrealMCPRecord> SetProgress =
        UMGEdit(AssetPath, Snapshot, TEXT("set_style"));
    SetProgress->SetStringField(TEXT("widget_id"), ProgressId);
    SetProgress->SetStringField(TEXT("property_name"), TEXT("Percent"));
    SetProgress->SetNumberField(TEXT("value"), 0.5);
    if (!TestTrue(
            TEXT("progress presentation succeeds"),
            Widgets.Execute(SetProgress, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    const FString BeforeOptions = Snapshot;
    TSharedRef<FUnrealMCPRecord> SetOptions =
        UMGEdit(AssetPath, Snapshot, TEXT("set_style"));
    SetOptions->SetStringField(TEXT("widget_id"), ComboId);
    SetOptions->SetStringField(TEXT("property_name"), TEXT("DefaultOptions"));
    SetOptions->SetArrayField(
        TEXT("value"),
        {
            MakeShared<FUnrealMCPValueString>(TEXT("Low")),
            MakeShared<FUnrealMCPValueString>(TEXT("Medium")),
            MakeShared<FUnrealMCPValueString>(TEXT("High")),
        });
    if (!TestTrue(
            TEXT("bounded combo-box options succeed"),
            Widgets.Execute(SetOptions, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FUnrealMCPRecord> UnsafeStyle =
        UMGEdit(AssetPath, Snapshot, TEXT("set_style"));
    UnsafeStyle->SetStringField(TEXT("widget_id"), ComboId);
    UnsafeStyle->SetStringField(TEXT("property_name"), TEXT("Slot"));
    UnsafeStyle->SetStringField(TEXT("value"), TEXT("unsafe"));
    TestFalse(
        TEXT("non-presentation widget state is rejected"),
        Widgets.Execute(UnsafeStyle, Result, Error));
    TestEqual(
        TEXT("unsupported style has a stable error"),
        Error.Code,
        FString(TEXT("unsupported_style")));
    TSharedRef<FUnrealMCPRecord> StaleStyle =
        UMGEdit(AssetPath, BeforeOptions, TEXT("set_style"));
    StaleStyle->SetStringField(TEXT("widget_id"), ComboId);
    StaleStyle->SetStringField(TEXT("property_name"), TEXT("SelectedOption"));
    StaleStyle->SetStringField(TEXT("value"), TEXT("Medium"));
    TestFalse(
        TEXT("stale presentation edit is rejected"),
        Widgets.Execute(StaleStyle, Result, Error));
    TestEqual(
        TEXT("stale presentation edit reports its precondition"),
        Error.Code,
        FString(TEXT("stale_precondition")));
    TestEqual(
        TEXT("rejected presentation edits preserve the snapshot"),
        InspectSnapshot(Inspector, AssetPath),
        Snapshot);

    for (const FString& WidgetId : {ProgressId, ButtonId})
    {
        TSharedRef<FUnrealMCPRecord> Expose =
            UMGEdit(AssetPath, Snapshot, TEXT("set_variable"));
        Expose->SetStringField(TEXT("widget_id"), WidgetId);
        Expose->SetBoolField(TEXT("is_variable"), true);
        if (!Widgets.Execute(Expose, Result, Error))
        {
            AddError(Error.Code + TEXT(": ") + Error.Message);
            return false;
        }
        Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    }
    TSharedRef<FUnrealMCPRecord> Compile = AssetArguments(AssetPath);
    Compile->SetStringField(
        TEXT("operation_id"),
        FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Compile->SetStringField(TEXT("expected_snapshot"), Snapshot);
    if (!TestTrue(
            TEXT("variable widgets compile"),
            Mutator.Execute(
                TEXT("blueprint_compile"), Compile, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    TSharedRef<FUnrealMCPRecord> BindProgress =
        UMGEdit(AssetPath, Snapshot, TEXT("bind_property"));
    BindProgress->SetStringField(TEXT("widget_id"), ProgressId);
    BindProgress->SetStringField(TEXT("target_property"), TEXT("Percent"));
    BindProgress->SetStringField(TEXT("source_kind"), TEXT("property"));
    BindProgress->SetStringField(TEXT("source_name"), TEXT("ResourceValue"));
    if (!TestTrue(
            TEXT("compatible typed property binding succeeds"),
            Widgets.Execute(BindProgress, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    TSharedRef<FUnrealMCPRecord> BindButton =
        UMGEdit(AssetPath, Snapshot, TEXT("bind_event"));
    BindButton->SetStringField(TEXT("widget_id"), ButtonId);
    BindButton->SetStringField(TEXT("delegate_name"), TEXT("OnClicked"));
    if (!TestTrue(
            TEXT("designer-created event handler succeeds"),
            Widgets.Execute(BindButton, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    const TSharedRef<FUnrealMCPRecord> Inspect = InspectArguments(AssetPath);
    Inspect->SetArrayField(
        TEXT("sections"),
        {
            MakeShared<FUnrealMCPValueString>(TEXT("widget_tree")),
            MakeShared<FUnrealMCPValueString>(TEXT("widget_bindings")),
        });
    if (!TestTrue(
            TEXT("UMG binding inspection succeeds"),
            Inspector.Execute(Inspect, Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message);
        return false;
    }
    TestTrue(
        TEXT("property binding reads back with widget identity"),
        HasBindingRecord(Result, ProgressId, TEXT("property_binding")));
    TestTrue(
        TEXT("event binding reads back with graph identity"),
        HasBindingRecord(Result, ButtonId, TEXT("event_binding")));

    TSharedRef<FUnrealMCPRecord> Save = AssetArguments(AssetPath);
    Save->SetStringField(
        TEXT("operation_id"),
        FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Save->SetStringField(TEXT("expected_snapshot"), Snapshot);
    TestTrue(
        TEXT("authored HUD saves"),
        Mutator.Execute(TEXT("blueprint_save"), Save, Result, Error));
    return true;
}

#endif
