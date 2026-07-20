// This translation unit intentionally includes every public Unreal API family
// needed by the roadmap before bridge contracts are frozen. Keeping it in the
// normal module build makes every supported engine compile the probe.
#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "FileHelpers.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
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
    static_assert(TIsDerivedFrom<USubobjectDataSubsystem, UEngineSubsystem>::Value);
    (void)FHttpServerModule::IsAvailable;
    (void)FAssetRegistryModule::GetRegistry;
    (void)FEditorFileUtils::SaveDirtyPackages;
}
}
