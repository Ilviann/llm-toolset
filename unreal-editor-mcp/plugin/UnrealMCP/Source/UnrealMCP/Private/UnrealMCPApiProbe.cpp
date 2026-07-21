// This translation unit intentionally includes every public Unreal API family
// needed by the roadmap before bridge contracts are frozen. Keeping it in the
// normal module build makes every supported engine compile the probe.
#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintActionFilter.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintEventNodeSpawner.h"
#include "BlueprintVariableNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "FileHelpers.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_PromotableOperator.h"
#include "K2Node_Tunnel.h"
#include "ScopedTransaction.h"
#include "SubobjectDataSubsystem.h"

namespace UnrealMCP::ApiProbe
{
void RequirePublicTypes()
{
    static_assert(sizeof(FScopedTransaction) > 0);
    static_assert(sizeof(FCompilerResultsLog) > 0);
    static_assert(TIsDerivedFrom<UEdGraphSchema_K2, UEdGraphSchema>::Value);
    static_assert(TIsDerivedFrom<UBlueprintNodeSpawner, UObject>::Value);
    static_assert(TIsDerivedFrom<UBlueprintFunctionNodeSpawner, UBlueprintNodeSpawner>::Value);
    static_assert(TIsDerivedFrom<UBlueprintEventNodeSpawner, UBlueprintNodeSpawner>::Value);
    static_assert(TIsDerivedFrom<UBlueprintVariableNodeSpawner, UBlueprintNodeSpawner>::Value);
    static_assert(TIsDerivedFrom<UK2Node_FunctionEntry, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_FunctionResult, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_CustomEvent, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_DynamicCast, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_IfThenElse, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_EnumLiteral, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_PromotableOperator, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_MacroInstance, UK2Node>::Value);
    static_assert(TIsDerivedFrom<UK2Node_Tunnel, UK2Node>::Value);
    static_assert(TIsDerivedFrom<USubobjectDataSubsystem, UEngineSubsystem>::Value);
    (void)FHttpServerModule::IsAvailable;
    (void)FAssetRegistryModule::GetRegistry;
    (void)FBlueprintActionDatabase::Get;
    (void)FEditorFileUtils::SaveDirtyPackages;
}
}
