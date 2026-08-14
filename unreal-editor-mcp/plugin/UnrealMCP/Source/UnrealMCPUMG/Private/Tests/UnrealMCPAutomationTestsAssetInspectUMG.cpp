#if WITH_DEV_AUTOMATION_TESTS

#include "UnrealMCPAutomationTestSupport.h"
#include "UnrealMCPAssetFamilyRegistry.h"
#include "UnrealMCPBlueprintMutator.h"
#include "UnrealMCPUMGInspectionAdapter.h"
#include "UnrealMCPWidgetTreeService.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/ComboBoxString.h"
#include "Components/NamedSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace
{
TSharedRef<FUnrealMCPRecord> Edit(
    const FString& Path,
    const FString& Snapshot,
    const FString& Operation)
{
    const TSharedRef<FUnrealMCPRecord> Result = UnrealMCP::Tests::AssetArguments(Path);
    Result->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Result->SetStringField(TEXT("expected_snapshot"), Snapshot);
    Result->SetStringField(TEXT("operation"), Operation);
    return Result;
}

TSharedRef<FUnrealMCPRecord> UMGInspectPanelTarget(const FString& ParentId)
{
    const TSharedRef<FUnrealMCPRecord> Result = MakeShared<FUnrealMCPRecord>();
    Result->SetStringField(TEXT("kind"), TEXT("panel"));
    Result->SetStringField(TEXT("parent_id"), ParentId);
    return Result;
}

const TSharedPtr<FUnrealMCPValue>* Find(
    const FUnrealMCPAssetFamilyDocumentBuilder& Document,
    const FString& Path)
{
    const FUnrealMCPAssetFamilyValueRecord* Record = Document.GetRecords().FindByPredicate(
        [&Path](const FUnrealMCPAssetFamilyValueRecord& Candidate) { return Candidate.Path == Path; });
    return Record != nullptr ? &Record->Value : nullptr;
}

TSharedPtr<FUnrealMCPRecord> ObjectAt(
    const FUnrealMCPAssetFamilyDocumentBuilder& Document,
    const FString& Path)
{
    const TSharedPtr<FUnrealMCPValue>* Value = Find(Document, Path);
    return Value != nullptr && Value->IsValid() ? (*Value)->AsObject() : nullptr;
}

bool Inspect(
    const FUnrealMCPAssetFamilyDescriptor& Descriptor,
    UWidgetBlueprint* Blueprint,
    const TArray<FString>& Selector,
    bool bPage,
    int32 PageSize,
    int32 PageIndex,
    FUnrealMCPAssetFamilyDocumentBuilder& OutDocument,
    FUnrealMCPError& OutError)
{
    const FString Snapshot = Descriptor.SnapshotBuilder(Blueprint);
    FUnrealMCPAssetFamilyInspectionContext Context;
    Context.Asset = Blueprint;
    Context.Identity = {Blueprint->GetPathName(), Snapshot};
    Context.Selector.Segments = Selector;
    Context.PageSize = PageSize;
    Context.PageIndex = PageIndex;
    Context.bHasPaging = bPage;
    FUnrealMCPAssetFamilySelectorRouter Router(Descriptor.Bounds);
    FUnrealMCPAssetFamilySnapshotBuilder SnapshotBuilder(Descriptor.Bounds);
    return Descriptor.InspectionAdapter->Inspect(
        Context, OutDocument, Router, SnapshotBuilder, OutError);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUnrealMCPAssetInspectUMGTest,
    "UnrealMCP.AssetInspect.UMGHierarchyLayoutBindingsAndExclusions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMCPAssetInspectUMGTest::RunTest(const FString& Parameters)
{
    using namespace UnrealMCP::Tests;
    const FString Package = TEXT("/Game/UnrealMCPTests/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/WBP_InspectUMG");
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    FUnrealMCPWidgetTreeService Widgets(Inspector);
    TSharedPtr<FUnrealMCPRecord> Result;
    FUnrealMCPError Error;
    if (!TestTrue(TEXT("Widget Blueprint creates"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(UUserWidget::StaticClass()->GetPathName(), Package), Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message); return false;
    }
    const FString AssetPath = Result->GetStringField(TEXT("asset_path"));
    UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
    if (!TestNotNull(TEXT("fixture is a Widget Blueprint"), Blueprint)) return false;
    FString Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    TSharedRef<FUnrealMCPRecord> Root = Edit(AssetPath, Snapshot, TEXT("set_root"));
    Root->SetStringField(TEXT("widget_class"), UCanvasPanel::StaticClass()->GetPathName());
    Root->SetStringField(TEXT("name"), TEXT("RootCanvas"));
    if (!TestTrue(TEXT("root creates"), Widgets.Execute(Root, Result, Error))) return false;
    const FString RootId = Result->GetStringField(TEXT("widget_id"));
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    auto AddWidget = [&](UClass* Class, const TCHAR* Name, FString& OutId)
    {
        TSharedRef<FUnrealMCPRecord> Add = Edit(AssetPath, Snapshot, TEXT("add"));
        Add->SetStringField(TEXT("widget_class"), Class->GetPathName());
        Add->SetStringField(TEXT("name"), Name);
        Add->SetObjectField(TEXT("target"), UMGInspectPanelTarget(RootId));
        if (!Widgets.Execute(Add, Result, Error)) return false;
        OutId = Result->GetStringField(TEXT("widget_id"));
        Snapshot = Result->GetStringField(TEXT("snapshot_id"));
        return true;
    };
    FString ProgressId, ButtonId, ComboId, NamedSlotId;
    if (!TestTrue(TEXT("ProgressBar creates"), AddWidget(UProgressBar::StaticClass(), TEXT("ResourceBar"), ProgressId))
        || !TestTrue(TEXT("Button creates"), AddWidget(UButton::StaticClass(), TEXT("PauseButton"), ButtonId))
        || !TestTrue(TEXT("ComboBox creates"), AddWidget(UComboBoxString::StaticClass(), TEXT("QualitySelector"), ComboId))
        || !TestTrue(TEXT("NamedSlot creates"), AddWidget(UNamedSlot::StaticClass(), TEXT("ExtensionPoint"), NamedSlotId)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message); return false;
    }
    TSharedRef<FUnrealMCPRecord> AddSlotContent = Edit(AssetPath, Snapshot, TEXT("add"));
    AddSlotContent->SetStringField(TEXT("widget_class"), UTextBlock::StaticClass()->GetPathName());
    AddSlotContent->SetStringField(TEXT("name"), TEXT("ExtensionLabel"));
    AddSlotContent->SetObjectField(TEXT("target"), UMGInspectPanelTarget(NamedSlotId));
    if (!TestTrue(TEXT("named slot content creates"), Widgets.Execute(AddSlotContent, Result, Error))) return false;
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    FEdGraphPinType FloatType;
    FloatType.PinCategory = UEdGraphSchema_K2::PC_Real;
    FloatType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
    TestTrue(TEXT("binding source member creates"),
        FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("ResourceValue"), FloatType, TEXT("0.5")));
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    Snapshot = InspectSnapshot(Inspector, AssetPath);
    for (const FString& WidgetId : {ProgressId, ButtonId})
    {
        TSharedRef<FUnrealMCPRecord> Variable = Edit(AssetPath, Snapshot, TEXT("set_variable"));
        Variable->SetStringField(TEXT("widget_id"), WidgetId);
        Variable->SetBoolField(TEXT("is_variable"), true);
        if (!TestTrue(TEXT("binding target exposes as variable"), Widgets.Execute(Variable, Result, Error))) return false;
        Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    }
    TSharedRef<FUnrealMCPRecord> Compile = AssetArguments(AssetPath);
    Compile->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Compile->SetStringField(TEXT("expected_snapshot"), Snapshot);
    if (!TestTrue(TEXT("variable widgets compile"), Mutator.Execute(TEXT("blueprint_compile"), Compile, Result, Error))) return false;
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));

    TSharedRef<FUnrealMCPRecord> PropertyBinding = Edit(AssetPath, Snapshot, TEXT("bind_property"));
    PropertyBinding->SetStringField(TEXT("widget_id"), ProgressId);
    PropertyBinding->SetStringField(TEXT("target_property"), TEXT("Percent"));
    PropertyBinding->SetStringField(TEXT("source_kind"), TEXT("property"));
    PropertyBinding->SetStringField(TEXT("source_name"), TEXT("ResourceValue"));
    if (!TestTrue(TEXT("legacy binding creates"), Widgets.Execute(PropertyBinding, Result, Error))) return false;
    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FUnrealMCPRecord> EventBinding = Edit(AssetPath, Snapshot, TEXT("bind_event"));
    EventBinding->SetStringField(TEXT("widget_id"), ButtonId);
    EventBinding->SetStringField(TEXT("delegate_name"), TEXT("OnClicked"));
    if (!TestTrue(TEXT("Designer event creates"), Widgets.Execute(EventBinding, Result, Error))) return false;

    UComboBoxString* Combo = Cast<UComboBoxString>(Blueprint->WidgetTree->FindWidget(TEXT("QualitySelector")));
    if (!TestNotNull(TEXT("ComboBox template resolves"), Combo)) return false;
    FArrayProperty* DefaultOptionsProperty = FindFProperty<FArrayProperty>(
        UComboBoxString::StaticClass(), TEXT("DefaultOptions"));
    if (!TestNotNull(TEXT("DefaultOptions authored property resolves"), DefaultOptionsProperty)) return false;
    FStrProperty* OptionProperty = CastField<FStrProperty>(DefaultOptionsProperty->Inner);
    if (!TestNotNull(TEXT("DefaultOptions stores strings"), OptionProperty)) return false;
    FScriptArrayHelper DefaultOptions(DefaultOptionsProperty,
        DefaultOptionsProperty->ContainerPtrToValuePtr<void>(Combo));
    DefaultOptions.Resize(2);
    OptionProperty->SetPropertyValue(DefaultOptions.GetRawPtr(0), TEXT("Low"));
    OptionProperty->SetPropertyValue(DefaultOptions.GetRawPtr(1), TEXT("High"));
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    FUnrealMCPAssetFamilyRegistry Registry;
    if (!TestTrue(TEXT("UMG adapter registers"), UnrealMCP::UMGInspection::RegisterAdapter(Registry, Error))
        || !TestTrue(TEXT("UMG adapter registry freezes"), Registry.Freeze(Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message); return false;
    }
    const FUnrealMCPAssetFamilyDescriptor& Descriptor = Registry.GetDescriptors()[0];
    TestTrue(TEXT("UMG family is a semantic overlay"), Descriptor.bComposableInspectionOverlay);
    const bool bDirtyBefore = Blueprint->GetOutermost()->IsDirty();

    FUnrealMCPAssetFamilyDocumentBuilder RootDocument(Descriptor.Bounds);
    if (!TestTrue(TEXT("UMG root inspects"), Inspect(
        Descriptor, Blueprint, {}, false, 10, 0, RootDocument, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message); return false;
    }
    TestTrue(TEXT("root publishes Widget Blueprint policy"), ObjectAt(RootDocument, TEXT("widget_blueprint")).IsValid());
    const TSharedPtr<FUnrealMCPRecord> Tree = ObjectAt(RootDocument, TEXT("widget_tree"));
    TestEqual(TEXT("root reports all widgets"), Tree->GetIntegerField(TEXT("widget_count")), 6);
    const TSharedPtr<FUnrealMCPRecord> Navigation =
        ObjectAt(RootDocument, TEXT("widget"))->GetObjectField(TEXT("navigation"));
    TestEqual(TEXT("root flattens default navigation"),
        Navigation->GetObjectField(TEXT("up"))->GetStringField(TEXT("rule")), FString(TEXT("escape")));
    TestFalse(TEXT("UMG overlay emits no animation block"), Find(RootDocument, TEXT("animations")) != nullptr);
    TestFalse(TEXT("UMG overlay emits no CommonUI block"), Find(RootDocument, TEXT("commonui_widget")) != nullptr);

    FUnrealMCPAssetFamilyDocumentBuilder TreePage(Descriptor.Bounds);
    TestTrue(TEXT("widget tree pages from zero"), Inspect(
        Descriptor, Blueprint, {TEXT("widget_tree")}, true, 2, 1, TreePage, Error));
    TestEqual(TEXT("second widget page has two records"),
        (*Find(TreePage, TEXT("widgets")))->AsArray().Num(), 2);

    FUnrealMCPAssetFamilyDocumentBuilder WidgetDocument(Descriptor.Bounds);
    TestTrue(TEXT("exact widget inspects"), Inspect(
        Descriptor, Blueprint, {TEXT("widgets"), TEXT("QualitySelector")}, false, 10, 0, WidgetDocument, Error));
    TestTrue(TEXT("exact widget has slot layout selector"),
        ObjectAt(WidgetDocument, TEXT("widget"))->GetObjectField(TEXT("slot")).IsValid());

    FUnrealMCPAssetFamilyDocumentBuilder OptionsDocument(Descriptor.Bounds);
    TestTrue(TEXT("collection-valued style property pages"), Inspect(
        Descriptor, Blueprint,
        {TEXT("widgets"), TEXT("QualitySelector"), TEXT("properties"), TEXT("DefaultOptions")},
        true, 1, 1, OptionsDocument, Error));
    TestEqual(TEXT("DefaultOptions second page returns one item"),
        (*Find(OptionsDocument, TEXT("items")))->AsArray().Num(), 1);

    FUnrealMCPAssetFamilyDocumentBuilder BindingsDocument(Descriptor.Bounds);
    TestTrue(TEXT("base bindings page inspects"), Inspect(
        Descriptor, Blueprint, {TEXT("bindings")}, true, 10, 0, BindingsDocument, Error));
    TestEqual(TEXT("legacy and Designer bindings are both projected"),
        (*Find(BindingsDocument, TEXT("bindings")))->AsArray().Num(), 2);
    FUnrealMCPAssetFamilyDocumentBuilder NamedSlotsDocument(Descriptor.Bounds);
    TestTrue(TEXT("named slot records inspect"), Inspect(
        Descriptor, Blueprint, {TEXT("named_slots")}, true, 10, 0, NamedSlotsDocument, Error));
    TestEqual(TEXT("authored named slot is projected"),
        (*Find(NamedSlotsDocument, TEXT("named_slots")))->AsArray().Num(), 1);
    TestEqual(TEXT("inspection preserves package dirty state"), Blueprint->GetOutermost()->IsDirty(), bDirtyBefore);

    const FString ChildPackage = Package + TEXT("_Child");
    if (!TestTrue(TEXT("derived Widget Blueprint creates"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(Blueprint->GeneratedClass->GetPathName(), ChildPackage), Result, Error)))
    {
        AddError(Error.Code + TEXT(": ") + Error.Message); return false;
    }
    UWidgetBlueprint* ChildBlueprint = LoadObject<UWidgetBlueprint>(
        nullptr, *(ChildPackage + TEXT(".") + FPackageName::GetLongPackageAssetName(ChildPackage)));
    if (!TestNotNull(TEXT("derived fixture resolves"), ChildBlueprint)) return false;
    FUnrealMCPAssetFamilyDocumentBuilder InheritedTree(Descriptor.Bounds);
    TestTrue(TEXT("inherited widget tree inspects"), Inspect(
        Descriptor, ChildBlueprint, {TEXT("widget_tree")}, true, 10, 0, InheritedTree, Error));
    const TArray<TSharedPtr<FUnrealMCPValue>>& InheritedWidgets =
        (*Find(InheritedTree, TEXT("widgets")))->AsArray();
    TestTrue(TEXT("derived view retains inherited hierarchy"), InheritedWidgets.Num() > 0);
    TestEqual(TEXT("derived view marks inherited ownership"),
        InheritedWidgets[0]->AsObject()->GetStringField(TEXT("ownership")), FString(TEXT("inherited")));
    return true;
}

#endif
