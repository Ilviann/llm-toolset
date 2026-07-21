#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneComponent.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture2D.h"
#include "Engine/SCS_Node.h"
#include "Engine/Selection.h"
#include "Engine/SimpleConstructionScript.h"
#include "Editor/Transactor.h"
#include "GameFramework/Actor.h"
#include "HttpPath.h"
#include "HttpRequestHandler.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPBlueprintMutator.h"
#include "UnrealMCPCompatibility.h"
#include "UnrealMCPProtocol.h"
#include "UnrealMCPTokenStore.h"
#include "UnrealMCPVersion.h"
#include "UObject/SavePackage.h"
#include "UObject/SoftObjectPath.h"

namespace
{
const FHttpPath Phase1RoutePath(TEXT("/unreal-mcp/v1/command"));

TSharedPtr<FHttpServerRequest> MakeRequest(const FString& Authorization, int32 BodyBytes = INDEX_NONE)
{
    const TSharedPtr<FHttpServerRequest> Request = MakeShared<FHttpServerRequest>();
    Request->RelativePath = Phase1RoutePath;
    Request->Verb = EHttpServerRequestVerbs::VERB_POST;
    Request->Headers.FindOrAdd(TEXT("authorization")).Add(Authorization);
    if (BodyBytes == INDEX_NONE)
    {
        const FString Json = TEXT("{\"command\":\"capabilities\",\"arguments\":{}}");
        FTCHARToUTF8 Encoded(*Json);
        Request->Body.Append(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
    }
    else
    {
        Request->Body.SetNumZeroed(BodyBytes);
    }
    return Request;
}

bool LoadLiveToken(FString& OutToken)
{
    if (!FFileHelper::LoadFileToString(OutToken, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMCP"), TEXT("bridge.token"))))
    {
        return false;
    }
    OutToken.TrimStartAndEndInline();
    return FUnrealMCPTokenStore::IsValidToken(OutToken);
}

TSharedRef<FJsonObject> InspectArguments(const FString& AssetPath, int32 PageSize = 100)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("mode"), TEXT("inspect"));
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    Arguments->SetNumberField(TEXT("page_size"), PageSize);
    return Arguments;
}

TSharedRef<FJsonObject> AllSectionArguments(const FString& AssetPath, int32 PageSize = 100)
{
    TSharedRef<FJsonObject> Arguments = InspectArguments(AssetPath, PageSize);
    TArray<TSharedPtr<FJsonValue>> Sections;
    for (const TCHAR* Name : {TEXT("summary"), TEXT("parent_class"), TEXT("compile_state"), TEXT("components"),
        TEXT("variables"), TEXT("graphs"), TEXT("nodes"), TEXT("pins"), TEXT("connections")})
    {
        Sections.Add(MakeShared<FJsonValueString>(Name));
    }
    Arguments->SetArrayField(TEXT("sections"), Sections);
    return Arguments;
}

UBlueprint* CreateBlueprintFixture(const FString& PackageName, UClass* ParentClass, bool bAddStructure)
{
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
    const FString ObjectPath = PackageName + TEXT(".") + AssetName;
    if (UBlueprint* Existing = FindObject<UBlueprint>(nullptr, *ObjectPath))
    {
        return Existing;
    }
    if (FPackageName::DoesPackageExist(PackageName))
    {
        if (UBlueprint* Existing = LoadObject<UBlueprint>(nullptr, *ObjectPath))
        {
            return Existing;
        }
    }
    UPackage* Package = CreatePackage(*PackageName);
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        ParentClass, Package, FName(*AssetName), BPTYPE_Normal, FName(TEXT("UnrealMCP.Phase2")));
    if (Blueprint == nullptr)
    {
        return nullptr;
    }
    FAssetRegistryModule::AssetCreated(Blueprint);
    if (bAddStructure)
    {
        FEdGraphPinType IntegerType;
        IntegerType.PinCategory = UEdGraphSchema_K2::PC_Int;
        FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Health"), IntegerType, TEXT("100"));
        FEdGraphPinType UnsupportedType;
        UnsupportedType.PinCategory = TEXT("unreal_mcp_unsupported");
        FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("UnsupportedValue"), UnsupportedType);
        if (Blueprint->SimpleConstructionScript != nullptr)
        {
            USCS_Node* Node = Blueprint->SimpleConstructionScript->CreateNode(USceneComponent::StaticClass(), TEXT("SceneRoot"));
            Blueprint->SimpleConstructionScript->AddNode(Node);
            if (USceneComponent* Template = Cast<USceneComponent>(Node->ComponentTemplate))
            {
                Template->SetRelativeLocation(FVector(10.0, 20.0, 30.0));
            }
        }
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Blueprint;
}

bool SaveBlueprintFixture(UBlueprint* Blueprint)
{
    if (Blueprint == nullptr) return false;
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Blueprint->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    SaveArgs.bSlowTask = false;
    return UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, SaveArgs);
}

bool ResultHasSection(const TSharedPtr<FJsonObject>& Result, const FString& Section)
{
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr) return false;
    for (const TSharedPtr<FJsonValue>& Item : *Records)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FString Value;
        if (Item.IsValid() && Item->TryGetObject(Object) && Object != nullptr && Object->IsValid()
            && (*Object)->TryGetStringField(TEXT("section"), Value) && Value == Section)
        {
            return true;
        }
    }
    return false;
}

bool ResultHasUnsupportedType(const TSharedPtr<FJsonObject>& Result)
{
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr) return false;
    for (const TSharedPtr<FJsonValue>& Item : *Records)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        const TSharedPtr<FJsonObject>* Type = nullptr;
        bool bSupported = true;
        if (Item.IsValid() && Item->TryGetObject(Object) && Object != nullptr && Object->IsValid()
            && (*Object)->TryGetObjectField(TEXT("type"), Type) && Type != nullptr && Type->IsValid()
            && (*Type)->TryGetBoolField(TEXT("supported"), bSupported) && !bSupported)
        {
            return true;
        }
    }
    return false;
}

TSharedRef<FJsonObject> CreateArguments(const FString& ParentClass, const FString& PackagePath)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("parent_class"), ParentClass);
    Arguments->SetStringField(TEXT("package_path"), PackagePath);
    return Arguments;
}

TSharedRef<FJsonObject> AssetArguments(const FString& AssetPath)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    return Arguments;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPTokenPersistenceTest, "UnrealMCP.Phase1.TokenPersistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPTokenPersistenceTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("UnrealMCPTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    FString First;
    FString Error;
    TestTrue(TEXT("token is created"), FUnrealMCPTokenStore::LoadOrCreate(Directory, First, Error));
    TestTrue(TEXT("token format is valid"), FUnrealMCPTokenStore::IsValidToken(First));
    FString Second;
    TestTrue(TEXT("token is reloaded"), FUnrealMCPTokenStore::LoadOrCreate(Directory, Second, Error));
    TestEqual(TEXT("persisted token is stable"), Second, First);
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPInvalidTokenFailsClosedTest, "UnrealMCP.Phase1.InvalidTokenFailsClosed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPInvalidTokenFailsClosedTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("UnrealMCPTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*Directory, true);
    FFileHelper::SaveStringToFile(TEXT("weak"), *FPaths::Combine(Directory, TEXT("bridge.token")));
    FString Token;
    FString Error;
    TestFalse(TEXT("invalid persisted token is rejected"), FUnrealMCPTokenStore::LoadOrCreate(Directory, Token, Error));
    TestTrue(TEXT("token is not returned"), Token.IsEmpty());
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPProtocolBoundsTest, "UnrealMCP.Phase1.ProtocolBounds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPProtocolBoundsTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("matching secrets compare"), UnrealMCP::Protocol::ConstantTimeEquals(TEXT("abc"), TEXT("abc")));
    TestFalse(TEXT("different secrets reject"), UnrealMCP::Protocol::ConstantTimeEquals(TEXT("abc"), TEXT("abd")));
    TestFalse(TEXT("different lengths reject"), UnrealMCP::Protocol::ConstantTimeEquals(TEXT("abc"), TEXT("ab")));
    const FString Json = TEXT("{\"command\":\"capabilities\",\"arguments\":{}}");
    FTCHARToUTF8 Encoded(*Json);
    TArray<uint8> Body(reinterpret_cast<const uint8*>(Encoded.Get()), Encoded.Length());
    FString Command;
    TSharedPtr<FJsonObject> Arguments;
    FUnrealMCPError Error;
    TestTrue(TEXT("valid command parses"), UnrealMCP::Protocol::ParseCommand(Body, Command, Arguments, Error));
    TestEqual(TEXT("command preserved"), Command, FString(TEXT("capabilities")));
    TArray<uint8> Large;
    Large.SetNumZeroed(UnrealMCP::MaxRequestBytes + 1);
    TestFalse(TEXT("oversized body rejects"), UnrealMCP::Protocol::ParseCommand(Large, Command, Arguments, Error));
    const FString DeepJson = TEXT("{\"command\":\"capabilities\",\"arguments\":") + FString::ChrN(20, TEXT('[')) + FString::ChrN(20, TEXT(']')) + TEXT("}");
    FTCHARToUTF8 DeepEncoded(*DeepJson);
    TArray<uint8> DeepBody(reinterpret_cast<const uint8*>(DeepEncoded.Get()), DeepEncoded.Length());
    TestFalse(TEXT("excessive JSON depth rejects"), UnrealMCP::Protocol::ParseCommand(DeepBody, Command, Arguments, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPErrorEnvelopeTest, "UnrealMCP.Phase1.ErrorEnvelope", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPErrorEnvelopeTest::RunTest(const FString& Parameters)
{
    TUniquePtr<FHttpServerResponse> Response = UnrealMCP::Protocol::Error(EHttpServerResponseCodes::Denied, TEXT("authentication_failed"), FString::ChrN(2000, TEXT('x')));
    TestTrue(TEXT("error response is bounded"), Response->Body.Num() <= UnrealMCP::MaxResponseBytes);
    FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Response->Body.GetData()), Response->Body.Num());
    TSharedPtr<FJsonObject> Root;
    TestTrue(TEXT("error response is JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Converted.Length(), Converted.Get())), Root));
    TestFalse(TEXT("error envelope reports failure"), Root->GetBoolField(TEXT("ok")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPCompatibilityBranchTest, "UnrealMCP.Phase1.CompatibilityBranch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPCompatibilityBranchTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("current public APIs are supported"), UnrealMCP::Compatibility::SupportsCurrentEngine());
    TestFalse(TEXT("API line is reported"), UnrealMCP::Compatibility::EngineApiLine().IsEmpty());
    TestTrue(TEXT("automation executes on Game thread"), IsInGameThread());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPRouteGuardsTest, "UnrealMCP.Phase1.RouteGuards", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPRouteGuardsTest::RunTest(const FString& Parameters)
{
    const TSharedPtr<IHttpRouter> Router = FHttpServerModule::Get().GetHttpRouter(UnrealMCP::DefaultPort, true);
    TestTrue(TEXT("live bridge router is available"), Router.IsValid());
    if (!Router.IsValid())
    {
        return false;
    }

    const FHttpRequestHandler DuplicateHandler = FHttpRequestHandler::CreateLambda([](const FHttpServerRequest&, const FHttpResultCallback&)
    {
        return true;
    });
    const FHttpRouteHandle DuplicateRoute = Router->BindRoute(Phase1RoutePath, EHttpServerRequestVerbs::VERB_POST, DuplicateHandler);
    TestFalse(TEXT("duplicate route ownership is rejected"), DuplicateRoute.IsValid());

    TUniquePtr<FHttpServerResponse> AuthenticationResponse;
    const bool bAuthenticationMatched = Router->Query(
        MakeRequest(TEXT("Bearer invalid")),
        [&AuthenticationResponse](TUniquePtr<FHttpServerResponse>&& Response)
        {
            AuthenticationResponse = MoveTemp(Response);
        });
    TestTrue(TEXT("authentication request reaches bridge route"), bAuthenticationMatched);
    TestTrue(TEXT("authentication rejection completes synchronously"), AuthenticationResponse.IsValid());
    if (AuthenticationResponse.IsValid())
    {
        TestEqual(TEXT("invalid authentication is denied"), static_cast<int32>(AuthenticationResponse->Code), static_cast<int32>(EHttpServerResponseCodes::Denied));
    }

    FString Token;
    TestTrue(TEXT("live token is readable"), LoadLiveToken(Token));
    TUniquePtr<FHttpServerResponse> BoundsResponse;
    const bool bBoundsMatched = Router->Query(
        MakeRequest(FString(TEXT("Bearer ")) + Token, UnrealMCP::MaxRequestBytes + 1),
        [&BoundsResponse](TUniquePtr<FHttpServerResponse>&& Response)
        {
            BoundsResponse = MoveTemp(Response);
        });
    TestTrue(TEXT("oversized request reaches bridge route"), bBoundsMatched);
    TestTrue(TEXT("oversized request completes synchronously"), BoundsResponse.IsValid());
    if (BoundsResponse.IsValid())
    {
        TestEqual(TEXT("oversized authenticated request is rejected"), static_cast<int32>(BoundsResponse->Code), static_cast<int32>(EHttpServerResponseCodes::RequestTooLarge));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPGameThreadDispatchTest, "UnrealMCP.Phase1.GameThreadDispatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPGameThreadDispatchTest::RunTest(const FString& Parameters)
{
    struct FDispatchState
    {
        bool bCompleted = false;
        bool bOnGameThread = false;
        int32 ResponseCode = static_cast<int32>(EHttpServerResponseCodes::Unknown);
    };

    const TSharedPtr<IHttpRouter> Router = FHttpServerModule::Get().GetHttpRouter(UnrealMCP::DefaultPort, true);
    TestTrue(TEXT("live bridge router is available"), Router.IsValid());
    FString Token;
    TestTrue(TEXT("live token is readable"), LoadLiveToken(Token));
    if (!Router.IsValid() || !FUnrealMCPTokenStore::IsValidToken(Token))
    {
        return false;
    }

    const TSharedRef<FDispatchState> State = MakeShared<FDispatchState>();
    const bool bMatched = Router->Query(
        MakeRequest(FString(TEXT("Bearer ")) + Token),
        [State](TUniquePtr<FHttpServerResponse>&& Response)
        {
            State->bOnGameThread = IsInGameThread();
            State->ResponseCode = Response.IsValid() ? static_cast<int32>(Response->Code) : static_cast<int32>(EHttpServerResponseCodes::Unknown);
            State->bCompleted = true;
        });
    TestTrue(TEXT("valid request reaches bridge route"), bMatched);
    TestFalse(TEXT("valid request is queued before callback"), State->bCompleted);

    AddCommand(new FDelayedFunctionLatentCommand(
        [this, State]()
        {
            TestTrue(TEXT("queued request completes"), State->bCompleted);
            TestTrue(TEXT("bridge callback runs on Game thread"), State->bOnGameThread);
            TestEqual(TEXT("valid request succeeds"), State->ResponseCode, static_cast<int32>(EHttpServerResponseCodes::Ok));
        },
        0.1f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase2InspectionTest, "UnrealMCP.Phase2.InspectionContracts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase2InspectionTest::RunTest(const FString& Parameters)
{
    const FString Base = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString PluginContentDirectory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("UnrealMCPPluginContent"));
    IFileManager::Get().MakeDirectory(*PluginContentDirectory, true);
    FPackageName::RegisterMountPoint(TEXT("/UnrealMCPTestPlugin/"), PluginContentDirectory + TEXT("/"));
    UBlueprint* ActorBlueprint = CreateBlueprintFixture(Base + TEXT("/BP_Actor"), AActor::StaticClass(), true);
    UBlueprint* ObjectBlueprint = CreateBlueprintFixture(Base + TEXT("/BP_Object"), UObject::StaticClass(), false);
    UBlueprint* ChildBlueprint = ActorBlueprint != nullptr
        ? CreateBlueprintFixture(Base + TEXT("/BP_Child"), ActorBlueprint->GeneratedClass, false) : nullptr;
    UPackage* PlainPackage = CreatePackage(*(Base + TEXT("/PlainAsset")));
    UObject* PlainAsset = NewObject<UTexture2D>(PlainPackage, TEXT("PlainAsset"), RF_Public | RF_Standalone);
    FAssetRegistryModule::AssetCreated(PlainAsset);
    UBlueprint* PluginBlueprint = CreateBlueprintFixture(TEXT("/UnrealMCPTestPlugin/BP_PluginActor"), AActor::StaticClass(), true);
    TestNotNull(TEXT("Actor Blueprint fixture is created"), ActorBlueprint);
    TestNotNull(TEXT("non-Actor Blueprint fixture is created"), ObjectBlueprint);
    TestNotNull(TEXT("inherited Actor Blueprint fixture is created"), ChildBlueprint);
    TestNotNull(TEXT("mounted plugin Actor Blueprint fixture is created"), PluginBlueprint);
    if (ActorBlueprint == nullptr || ObjectBlueprint == nullptr || ChildBlueprint == nullptr || PluginBlueprint == nullptr)
    {
        FPackageName::UnRegisterMountPoint(TEXT("/UnrealMCPTestPlugin/"), PluginContentDirectory + TEXT("/"));
        return false;
    }

    FUnrealMCPBlueprintInspector Inspector;
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    const FString ActorPath = ActorBlueprint->GetPathName();
    const bool bDirtyBefore = ActorBlueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBefore = ActorBlueprint->Status;
    const int32 SelectionBefore = GEditor != nullptr ? GEditor->GetSelectedObjects()->Num() : 0;
    const int32 TransactionsBefore = GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0;
    TestTrue(TEXT("complete structure inspection succeeds"), Inspector.Execute(AllSectionArguments(ActorPath), Result, Error));
    for (const TCHAR* Section : {TEXT("summary"), TEXT("parent_class"), TEXT("compile_state"), TEXT("component"),
        TEXT("variable"), TEXT("graph"), TEXT("node"), TEXT("pin")})
    {
        TestTrue(FString::Printf(TEXT("inspection includes %s"), Section), ResultHasSection(Result, Section));
    }
    TestTrue(TEXT("unsupported K2 types are explicit"), ResultHasUnsupportedType(Result));
    TestEqual(TEXT("inspection preserves package dirty state"), ActorBlueprint->GetOutermost()->IsDirty(), bDirtyBefore);
    TestEqual(TEXT("inspection preserves compile state"), ActorBlueprint->Status, StatusBefore);
    TestEqual(TEXT("inspection preserves selection"), GEditor != nullptr ? GEditor->GetSelectedObjects()->Num() : 0, SelectionBefore);
    TestEqual(TEXT("inspection creates no transaction"), GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0, TransactionsBefore);

    const TSharedRef<FJsonObject> Discover = MakeShared<FJsonObject>();
    Discover->SetStringField(TEXT("mode"), TEXT("discover"));
    Discover->SetStringField(TEXT("package_path"), FPackageName::GetLongPackagePath(Base));
    Discover->SetStringField(TEXT("asset_name"), ActorBlueprint->GetName());
    TestTrue(TEXT("exact discovery succeeds"), Inspector.Execute(Discover, Result, Error));
    TestTrue(TEXT("exact discovery finds Actor Blueprint"), ResultHasSection(Result, TEXT("asset")));

    Discover->RemoveField(TEXT("package_path"));
    Discover->SetStringField(TEXT("asset_name"), PluginBlueprint->GetName());
    TestTrue(TEXT("all-mount discovery succeeds"), Inspector.Execute(Discover, Result, Error));
    TestTrue(TEXT("all-mount discovery finds plugin Actor Blueprint"), ResultHasSection(Result, TEXT("asset")));
    Discover->SetStringField(TEXT("package_path"), TEXT("/UnrealMCPTestPlugin"));
    TestTrue(TEXT("plugin-mount discovery succeeds"), Inspector.Execute(Discover, Result, Error));
    TestTrue(TEXT("plugin-mount discovery finds Actor Blueprint"), ResultHasSection(Result, TEXT("asset")));
    TestTrue(TEXT("plugin-mount deep inspection succeeds"), Inspector.Execute(AllSectionArguments(PluginBlueprint->GetPathName()), Result, Error));
    TestTrue(TEXT("plugin-mount deep inspection returns structure"), ResultHasSection(Result, TEXT("graph")));

    Discover->SetStringField(TEXT("package_path"), FPackageName::GetLongPackagePath(Base));
    Discover->SetStringField(TEXT("asset_name"), ObjectBlueprint->GetName());
    TestTrue(TEXT("non-Actor discovery is a valid empty query"), Inspector.Execute(Discover, Result, Error));
    TestFalse(TEXT("non-Actor Blueprint is excluded from discovery"), ResultHasSection(Result, TEXT("asset")));
    TestFalse(TEXT("non-Actor deep inspection is rejected"), Inspector.Execute(InspectArguments(ObjectBlueprint->GetPathName()), Result, Error));
    TestEqual(TEXT("non-Actor error is stable"), Error.Code, FString(TEXT("wrong_type")));
    TestFalse(TEXT("missing asset is rejected"), Inspector.Execute(InspectArguments(Base + TEXT("/Missing.Missing")), Result, Error));
    TestEqual(TEXT("missing error is stable"), Error.Code, FString(TEXT("not_found")));
    TestFalse(TEXT("non-Blueprint asset is rejected"), Inspector.Execute(InspectArguments(PlainAsset->GetPathName()), Result, Error));
    TestEqual(TEXT("non-Blueprint error is stable"), Error.Code, FString(TEXT("wrong_type")));

    TSharedRef<FJsonObject> Inherited = InspectArguments(ChildBlueprint->GetPathName());
    Inherited->SetBoolField(TEXT("include_inherited"), true);
    Inherited->SetArrayField(TEXT("sections"), {
        MakeShared<FJsonValueString>(TEXT("components")), MakeShared<FJsonValueString>(TEXT("variables"))});
    TestTrue(TEXT("inherited inspection succeeds"), Inspector.Execute(Inherited, Result, Error));
    TestTrue(TEXT("inherited component is reported"), ResultHasSection(Result, TEXT("component")));
    TestTrue(TEXT("inherited variable is reported"), ResultHasSection(Result, TEXT("variable")));

    UBlueprint* EmptyGraphBlueprint = CreateBlueprintFixture(Base + TEXT("/BP_EmptyGraphs"), AActor::StaticClass(), false);
    EmptyGraphBlueprint->UbergraphPages.Empty();
    EmptyGraphBlueprint->FunctionGraphs.Empty();
    EmptyGraphBlueprint->MacroGraphs.Empty();
    EmptyGraphBlueprint->DelegateSignatureGraphs.Empty();
    TestTrue(TEXT("empty graph inspection succeeds"), Inspector.Execute(AllSectionArguments(EmptyGraphBlueprint->GetPathName()), Result, Error));
    TestFalse(TEXT("empty graph inspection returns no graph record"), ResultHasSection(Result, TEXT("graph")));

    UBlueprint* LargeBlueprint = CreateBlueprintFixture(Base + TEXT("/BP_Large"), AActor::StaticClass(), false);
    UEdGraph* LargeGraph = LargeBlueprint->UbergraphPages[0];
    for (int32 Index = 0; Index <= UnrealMCP::MaxInspectRecords; ++Index)
    {
        UEdGraphNode* Node = NewObject<UEdGraphNode>(LargeGraph);
        Node->CreateNewGuid();
        LargeGraph->AddNode(Node, false, false);
    }
    TestFalse(TEXT("oversized synthetic graph rejects"), Inspector.Execute(InspectArguments(LargeBlueprint->GetPathName()), Result, Error));
    TestEqual(TEXT("oversized graph error is stable"), Error.Code, FString(TEXT("response_too_large")));
    FPackageName::UnRegisterMountPoint(TEXT("/UnrealMCPTestPlugin/"), PluginContentDirectory + TEXT("/"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase2CursorTest, "UnrealMCP.Phase2.CursorGuards", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase2CursorTest::RunTest(const FString& Parameters)
{
    const FString PackageName = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Cursor");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), true);
    if (!TestNotNull(TEXT("cursor Blueprint fixture is created"), Blueprint)) return false;
    double CurrentTime = 10.0;
    FUnrealMCPBlueprintInspector Inspector([&CurrentTime] { return CurrentTime; });
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    TestTrue(TEXT("first small page succeeds"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName(), 1), Result, Error));
    FString Cursor;
    TestTrue(TEXT("first page returns opaque cursor"), Result->TryGetStringField(TEXT("next_cursor"), Cursor));
    const TSharedRef<FJsonObject> Continue = MakeShared<FJsonObject>();
    Continue->SetStringField(TEXT("cursor"), Cursor);
    TestTrue(TEXT("matching cursor continues"), Inspector.Execute(Continue, Result, Error));

    TestTrue(TEXT("fresh cursor page succeeds"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName(), 1), Result, Error));
    Result->TryGetStringField(TEXT("next_cursor"), Cursor);
    Blueprint->UbergraphPages[0]->Nodes[0]->NodePosX += 1;
    Continue->SetStringField(TEXT("cursor"), Cursor);
    TestFalse(TEXT("structurally stale cursor rejects"), Inspector.Execute(Continue, Result, Error));
    TestEqual(TEXT("stale cursor error is stable"), Error.Code, FString(TEXT("stale_precondition")));

    TestTrue(TEXT("another fresh cursor page succeeds"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName(), 1), Result, Error));
    Result->TryGetStringField(TEXT("next_cursor"), Cursor);
    CurrentTime += UnrealMCP::CursorLifetimeSeconds + 1.0;
    Continue->SetStringField(TEXT("cursor"), Cursor);
    TestFalse(TEXT("expired cursor rejects"), Inspector.Execute(Continue, Result, Error));
    TestEqual(TEXT("expired cursor error is stable"), Error.Code, FString(TEXT("cursor_expired")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase2LiveFixtureTest, "UnrealMCP.Phase2.LiveFixture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase2LiveFixtureTest::RunTest(const FString& Parameters)
{
    UBlueprint* Blueprint = CreateBlueprintFixture(TEXT("/Game/UnrealMCPPhase2/BP_InspectionFixture"), AActor::StaticClass(), true);
    if (!TestNotNull(TEXT("live Actor Blueprint fixture is available"), Blueprint)) return false;
    FUnrealMCPBlueprintInspector Inspector;
    TSharedPtr<FJsonObject> BeforeSave;
    FUnrealMCPError Error;
    TestTrue(TEXT("fixture inspects before save"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName()), BeforeSave, Error));
    const FString SnapshotBefore = BeforeSave.IsValid() ? BeforeSave->GetStringField(TEXT("snapshot_id")) : FString();
    if (Blueprint->UbergraphPages.Num() > 0 && Blueprint->UbergraphPages[0]->Nodes.Num() > 0 && GEditor != nullptr)
    {
        UEdGraph* Graph = Blueprint->UbergraphPages[0];
        UEdGraphNode* Node = Graph->Nodes[0];
        {
            const FScopedTransaction Transaction(FText::FromString(TEXT("Unreal MCP identity undo test")));
            Graph->Modify();
            Node->Modify();
            Node->NodePosX += 64;
        }
        TestTrue(TEXT("test transaction undoes"), GEditor->UndoTransaction());
        TSharedPtr<FJsonObject> AfterUndo;
        TestTrue(TEXT("fixture inspects after undo"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName()), AfterUndo, Error));
        TestEqual(TEXT("undo restores structural snapshot"), AfterUndo->GetStringField(TEXT("snapshot_id")), SnapshotBefore);
    }
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    TSharedPtr<FJsonObject> AfterCompile;
    TestTrue(TEXT("fixture inspects after reconstruction compile"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName()), AfterCompile, Error));
    TestEqual(TEXT("compile preserves available identities"), AfterCompile->GetStringField(TEXT("snapshot_id")), SnapshotBefore);
    TestTrue(TEXT("fixture saves without UI"), SaveBlueprintFixture(Blueprint));
    TSharedPtr<FJsonObject> AfterSave;
    TestTrue(TEXT("fixture inspects after save"), Inspector.Execute(AllSectionArguments(Blueprint->GetPathName()), AfterSave, Error));
    const FString SnapshotAfter = AfterSave.IsValid() ? AfterSave->GetStringField(TEXT("snapshot_id")) : FString();
    TestEqual(TEXT("save preserves structural snapshot"), SnapshotAfter, SnapshotBefore);
    UE_LOG(LogTemp, Display, TEXT("UNREAL_MCP_PHASE2_SNAPSHOT=%s"), *SnapshotAfter);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase3CreationTest, "UnrealMCP.Phase3.CreationContracts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase3CreationTest::RunTest(const FString& Parameters)
{
    const FString Base = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString ParentPackage = Base + TEXT("/BP_Created");
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    TestTrue(TEXT("native Actor Blueprint creation succeeds"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), ParentPackage), Result, Error));
    const FString ParentObject = ParentPackage + TEXT(".BP_Created");
    TestEqual(TEXT("created asset path is exact"), Result->GetStringField(TEXT("asset_path")), ParentObject);
    TestEqual(TEXT("created parent identity is exact"), Result->GetStringField(TEXT("parent_class")), FString(TEXT("/Script/Engine.Actor")));
    TestTrue(TEXT("creation compiles"), Result->GetBoolField(TEXT("compile_succeeded")));
    TestTrue(TEXT("creation saves"), Result->GetBoolField(TEXT("saved")));
    TestFalse(TEXT("saved creation is clean"), Result->GetBoolField(TEXT("package_dirty")));
    TestEqual(TEXT("creation returns a structural snapshot"), Result->GetStringField(TEXT("snapshot_id")).Len(), 40);

    UBlueprint* ParentBlueprint = LoadObject<UBlueprint>(nullptr, *ParentObject);
    TestNotNull(TEXT("created Blueprint loads"), ParentBlueprint);
    if (ParentBlueprint == nullptr)
    {
        return false;
    }
    const FString ChildPackage = Base + TEXT("/BP_Child");
    TestTrue(TEXT("Blueprint-generated Actor parent is accepted"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(ParentBlueprint->GeneratedClass->GetPathName(), ChildPackage), Result, Error));
    TestEqual(TEXT("generated parent identity is retained"), Result->GetStringField(TEXT("parent_class")), ParentBlueprint->GeneratedClass->GetPathName());

    TestFalse(TEXT("existing destination is never overwritten"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), ParentPackage), Result, Error));
    TestEqual(TEXT("duplicate error is stable"), Error.Code, FString(TEXT("already_exists")));
    TestNotNull(TEXT("existing destination remains available"), LoadObject<UBlueprint>(nullptr, *ParentObject));
    TestFalse(TEXT("case-only duplicate destination is rejected"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), ParentPackage.ToLower()), Result, Error));
    TestEqual(TEXT("case-only duplicate error is stable"), Error.Code, FString(TEXT("already_exists")));

    TestFalse(TEXT("non-Actor parent rejects"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/CoreUObject.Object"), Base + TEXT("/BP_Invalid")), Result, Error));
    TestEqual(TEXT("invalid parent error is stable"), Error.Code, FString(TEXT("invalid_parent")));
    TestFalse(TEXT("abstract Actor parent rejects"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Light"), Base + TEXT("/BP_Abstract")), Result, Error));
    TestEqual(TEXT("abstract parent error is stable"), Error.Code, FString(TEXT("invalid_parent")));
    TestFalse(TEXT("Blueprint skeleton parent rejects"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(ParentBlueprint->SkeletonGeneratedClass->GetPathName(), Base + TEXT("/BP_Skeleton")), Result, Error));
    TestEqual(TEXT("skeleton parent error is stable"), Error.Code, FString(TEXT("invalid_parent")));
    TestFalse(TEXT("engine mutation rejects"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), TEXT("/Engine/UnrealMCP/BP_Forbidden")), Result, Error));
    TestEqual(TEXT("engine scope error is stable"), Error.Code, FString(TEXT("mutation_scope_denied")));
    TestFalse(TEXT("object-suffixed package destination rejects"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), Base + TEXT("/BP_Bad.BP_Bad")), Result, Error));
    TestEqual(TEXT("invalid package path error is stable"), Error.Code, FString(TEXT("invalid_argument")));

    const FString ExternalDirectory = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("UnrealMCPExternalContent"));
    IFileManager::Get().MakeDirectory(*ExternalDirectory, true);
    FPackageName::RegisterMountPoint(TEXT("/UnrealMCPExternal/"), ExternalDirectory + TEXT("/"));
    TestFalse(TEXT("external plugin-style mount rejects mutation"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), TEXT("/UnrealMCPExternal/BP_Forbidden")), Result, Error));
    TestEqual(TEXT("external mount error is stable"), Error.Code, FString(TEXT("mutation_scope_denied")));
    FPackageName::UnRegisterMountPoint(TEXT("/UnrealMCPExternal/"), ExternalDirectory + TEXT("/"));

    const FString LocalPluginRoot = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("UnrealMCPPhase3TestPlugin"));
    const FString LocalPluginDirectory = FPaths::Combine(LocalPluginRoot, TEXT("Content"));
    IFileManager::Get().MakeDirectory(*LocalPluginDirectory, true);
    TestTrue(TEXT("local plugin descriptor is written"), FFileHelper::SaveStringToFile(
        TEXT("{\"FileVersion\":3}"), *FPaths::Combine(LocalPluginRoot, TEXT("UnrealMCPPhase3TestPlugin.uplugin"))));
    FPackageName::RegisterMountPoint(TEXT("/UnrealMCPPhase3Plugin/"), LocalPluginDirectory + TEXT("/"));
    const FString LocalPluginPackage = TEXT("/UnrealMCPPhase3Plugin/BP_Local_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TestTrue(TEXT("local project-plugin content creation succeeds"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), LocalPluginPackage), Result, Error));
    // Keep the dynamic mount alive through the editor's deferred validate-on-save pass.

    TestTrue(TEXT("explicit compile succeeds"), Mutator.Execute(TEXT("blueprint_compile"), AssetArguments(ParentObject), Result, Error));
    TestTrue(TEXT("explicit compile reports success"), Result->GetBoolField(TEXT("compile_succeeded")));
    TestFalse(TEXT("compile does not claim save"), Result->GetBoolField(TEXT("saved")));
    TestTrue(TEXT("explicit save succeeds"), Mutator.Execute(TEXT("blueprint_save"), AssetArguments(ParentObject), Result, Error));
    TestTrue(TEXT("explicit save reports save"), Result->GetBoolField(TEXT("saved")));
    TestFalse(TEXT("explicit save leaves package clean"), Result->GetBoolField(TEXT("package_dirty")));

    const FString ExistingFilename = FPackageName::LongPackageNameToFilename(ParentPackage, FPackageName::GetAssetPackageExtension());
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    TestTrue(TEXT("existing package becomes read-only"), PlatformFile.SetReadOnly(*ExistingFilename, true));
    TestFalse(TEXT("read-only existing package rejects save"), Mutator.Execute(
        TEXT("blueprint_save"), AssetArguments(ParentObject), Result, Error));
    TestEqual(TEXT("read-only save error is distinct"), Error.Code, FString(TEXT("write_conflict")));
    TestTrue(TEXT("existing package permissions are restored"), PlatformFile.SetReadOnly(*ExistingFilename, false));

    const FString ReadOnlyDirectory = FPackageName::LongPackageNameToFilename(Base + TEXT("/ReadOnly"));
    IFileManager::Get().MakeDirectory(*ReadOnlyDirectory, true);
    TestTrue(TEXT("test directory becomes read-only"), PlatformFile.SetReadOnly(*ReadOnlyDirectory, true));
    TestFalse(TEXT("read-only creation rejects before package creation"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), Base + TEXT("/ReadOnly/BP_NoWrite")), Result, Error));
    TestEqual(TEXT("read-only error is distinct"), Error.Code, FString(TEXT("write_conflict")));
    TestTrue(TEXT("test directory permissions are restored"), PlatformFile.SetReadOnly(*ReadOnlyDirectory, false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase3FailureTest, "UnrealMCP.Phase3.FailureCleanup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase3FailureTest::RunTest(const FString& Parameters)
{
    const FString Base = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FUnrealMCPBlueprintInspector Inspector;
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    const FString CompileFailurePackage = Base + TEXT("/BP_CompileFailure");
    FUnrealMCPBlueprintMutator CompileFailure(Inspector,
        [](UBlueprint* Blueprint, FCompilerResultsLog& Log)
        {
            Log.Error(TEXT("Synthetic bounded compile failure"));
            Blueprint->Status = BS_Error;
        });
    TestFalse(TEXT("creation compile failure is reported"), CompileFailure.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), CompileFailurePackage), Result, Error));
    TestEqual(TEXT("creation compile error is distinct"), Error.Code, FString(TEXT("compile_failed")));
    TestFalse(TEXT("compile-failed creation is not registered"), FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(
        FSoftObjectPath(CompileFailurePackage + TEXT(".BP_CompileFailure"))).IsValid());
    FString CompileFailureFilename;
    FPackageName::TryConvertLongPackageNameToFilename(CompileFailurePackage, CompileFailureFilename, FPackageName::GetAssetPackageExtension());
    TestFalse(TEXT("compile-failed creation leaves no file"), IFileManager::Get().FileExists(*CompileFailureFilename));

    const FString SaveFailurePackage = Base + TEXT("/BP_SaveFailure");
    FUnrealMCPBlueprintMutator SaveFailure(Inspector, FUnrealMCPBlueprintMutator::FCompile(), [](UBlueprint*) { return false; });
    TestFalse(TEXT("creation save failure is reported"), SaveFailure.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), SaveFailurePackage), Result, Error));
    TestEqual(TEXT("creation save error is distinct"), Error.Code, FString(TEXT("save_failed")));
    TestFalse(TEXT("save-failed creation is not registered"), FAssetRegistryModule::GetRegistry().GetAssetByObjectPath(
        FSoftObjectPath(SaveFailurePackage + TEXT(".BP_SaveFailure"))).IsValid());

    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TestTrue(TEXT("compile-failed destination can be retried"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), CompileFailurePackage), Result, Error));
    TestTrue(TEXT("save-failed destination can be retried"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), SaveFailurePackage), Result, Error));

    const FString ExistingObject = CompileFailurePackage + TEXT(".BP_CompileFailure");
    FUnrealMCPBlueprintMutator ExplicitCompileFailure(Inspector,
        [](UBlueprint* Blueprint, FCompilerResultsLog& Log)
        {
            Log.Error(TEXT("Synthetic explicit compile failure"));
            Blueprint->Status = BS_Error;
        });
    TestTrue(TEXT("explicit compile failure returns structured result"), ExplicitCompileFailure.Execute(
        TEXT("blueprint_compile"), AssetArguments(ExistingObject), Result, Error));
    TestFalse(TEXT("explicit compile result reports failure"), Result->GetBoolField(TEXT("compile_succeeded")));
    TestTrue(TEXT("explicit compile result contains diagnostics"), Result->GetIntegerField(TEXT("diagnostic_count")) > 0);
    TestTrue(TEXT("normal compile restores fixture"), Mutator.Execute(TEXT("blueprint_compile"), AssetArguments(ExistingObject), Result, Error));

    FUnrealMCPBlueprintMutator ExplicitSaveFailure(Inspector, FUnrealMCPBlueprintMutator::FCompile(), [](UBlueprint*) { return false; });
    TestFalse(TEXT("explicit save failure is reported"), ExplicitSaveFailure.Execute(
        TEXT("blueprint_save"), AssetArguments(ExistingObject), Result, Error));
    TestEqual(TEXT("explicit save error is distinct"), Error.Code, FString(TEXT("save_failed")));
    TestNotNull(TEXT("save failure preserves existing asset"), LoadObject<UBlueprint>(nullptr, *ExistingObject));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase3LiveFixtureTest, "UnrealMCP.Phase3.CreationLiveFixture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase3LiveFixtureTest::RunTest(const FString& Parameters)
{
    const FString PackageName = TEXT("/Game/UnrealMCPPhase3/BP_CreationFixture");
    const FString ObjectPath = PackageName + TEXT(".BP_CreationFixture");
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    TestTrue(TEXT("live creation fixture is created through production mutator"), Mutator.Execute(
        TEXT("blueprint_create"), CreateArguments(TEXT("/Script/Engine.Actor"), PackageName), Result, Error));
    if (!Result.IsValid())
    {
        return false;
    }
    const FString Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TestEqual(TEXT("live fixture snapshot is bounded"), Snapshot.Len(), 40);
    UE_LOG(LogTemp, Display, TEXT("UNREAL_MCP_PHASE3_SNAPSHOT=%s"), *Snapshot);
    return true;
}

#endif
