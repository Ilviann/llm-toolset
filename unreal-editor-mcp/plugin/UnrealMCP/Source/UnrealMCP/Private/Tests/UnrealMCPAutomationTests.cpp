#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SceneComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "GameFramework/RotatingMovementComponent.h"
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
#include "GameFramework/InputSettings.h"
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
#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealMCPBlueprintInspector.h"
#include "UnrealMCPBlueprintMutator.h"
#include "UnrealMCPCompatibility.h"
#include "UnrealMCPProtocol.h"
#include "UnrealMCPOperationLedger.h"
#include "UnrealMCPPropertyCodec.h"
#include "UnrealMCPK2TypeCodec.h"
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
        TEXT("variables"), TEXT("functions"), TEXT("parameters"), TEXT("local_variables"),
        TEXT("graphs"), TEXT("nodes"), TEXT("pins"), TEXT("connections")})
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

FString InspectSnapshot(FUnrealMCPBlueprintInspector& Inspector, const FString& AssetPath)
{
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    return Inspector.Execute(InspectArguments(AssetPath), Result, Error) && Result.IsValid()
        ? Result->GetStringField(TEXT("snapshot_id")) : FString();
}

TSharedRef<FJsonObject> ComponentEditArguments(const FString& AssetPath, const FString& Snapshot, const FString& Operation)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    Arguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
    Arguments->SetStringField(TEXT("operation"), Operation);
    return Arguments;
}

FString ComponentIdByName(FUnrealMCPBlueprintInspector& Inspector, const FString& AssetPath, const FString& Name)
{
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    const TSharedRef<FJsonObject> Arguments = InspectArguments(AssetPath);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("components"))});
    if (!Inspector.Execute(Arguments, Result, Error) || !Result.IsValid()) return FString();
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr) return FString();
    for (const TSharedPtr<FJsonValue>& Item : *Records)
    {
        const TSharedPtr<FJsonObject>* RecordObject = nullptr;
        FString RecordName;
        FString Id;
        if (Item.IsValid() && Item->TryGetObject(RecordObject) && RecordObject != nullptr && RecordObject->IsValid()
            && (*RecordObject)->TryGetStringField(TEXT("name"), RecordName) && RecordName == Name
            && (*RecordObject)->TryGetStringField(TEXT("id"), Id)) return Id;
    }
    return FString();
}

TSharedRef<FJsonObject> MemberEditArguments(const FString& AssetPath, const FString& Snapshot, const FString& Operation)
{
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    Arguments->SetStringField(TEXT("asset_path"), AssetPath);
    Arguments->SetStringField(TEXT("expected_snapshot"), Snapshot);
    Arguments->SetStringField(TEXT("operation"), Operation);
    return Arguments;
}

TSharedRef<FJsonObject> ScopedMemberEditArguments(
    const FString& AssetPath,
    const FString& Snapshot,
    const FString& Target,
    const FString& Operation)
{
    TSharedRef<FJsonObject> Arguments = MemberEditArguments(AssetPath, Snapshot, Operation);
    Arguments->SetStringField(TEXT("target"), Target);
    return Arguments;
}

TSharedRef<FJsonObject> K2Type(const FString& Category, const FString& Container = TEXT("none"))
{
    const TSharedRef<FJsonObject> Type = MakeShared<FJsonObject>();
    Type->SetStringField(TEXT("category"), Category);
    Type->SetStringField(TEXT("container"), Container);
    return Type;
}

TSharedRef<FJsonObject> LiteralDefault(const TSharedPtr<FJsonValue>& Value)
{
    const TSharedRef<FJsonObject> Default = MakeShared<FJsonObject>();
    Default->SetStringField(TEXT("kind"), TEXT("literal"));
    Default->SetField(TEXT("value"), Value);
    return Default;
}

TSharedRef<FJsonObject> FunctionParameter(
    const FString& Name,
    const FString& Direction,
    const TSharedRef<FJsonObject>& Type,
    const TSharedPtr<FJsonObject>& Default = nullptr)
{
    const TSharedRef<FJsonObject> Parameter = MakeShared<FJsonObject>();
    Parameter->SetStringField(TEXT("name"), Name);
    Parameter->SetStringField(TEXT("direction"), Direction);
    Parameter->SetObjectField(TEXT("type"), Type);
    if (Default.IsValid()) Parameter->SetObjectField(TEXT("default"), Default);
    return Parameter;
}

TSharedRef<FJsonObject> FunctionSignature(
    const FString& Access,
    bool bPure,
    bool bConst,
    const TArray<TSharedPtr<FJsonValue>>& Parameters)
{
    const TSharedRef<FJsonObject> Signature = MakeShared<FJsonObject>();
    Signature->SetStringField(TEXT("access"), Access);
    Signature->SetBoolField(TEXT("pure"), bPure);
    Signature->SetBoolField(TEXT("const"), bConst);
    Signature->SetArrayField(TEXT("parameters"), Parameters);
    return Signature;
}

FString ScopedIdByName(
    FUnrealMCPBlueprintInspector& Inspector,
    const FString& AssetPath,
    const FString& Section,
    const FString& Name)
{
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    const TSharedRef<FJsonObject> Arguments = InspectArguments(AssetPath);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(Section)});
    if (!Inspector.Execute(Arguments, Result, Error) || !Result.IsValid()) return FString();
    for (const TSharedPtr<FJsonValue>& Item : Result->GetArrayField(TEXT("records")))
    {
        const TSharedPtr<FJsonObject>* RecordObject = nullptr;
        FString RecordName;
        FString Id;
        if (Item.IsValid() && Item->TryGetObject(RecordObject) && RecordObject != nullptr && RecordObject->IsValid()
            && (*RecordObject)->TryGetStringField(TEXT("name"), RecordName) && RecordName == Name
            && (*RecordObject)->TryGetStringField(TEXT("id"), Id)) return Id;
    }
    return FString();
}

FString MemberIdByName(FUnrealMCPBlueprintInspector& Inspector, const FString& AssetPath, const FString& Name)
{
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;
    const TSharedRef<FJsonObject> Arguments = InspectArguments(AssetPath);
    Arguments->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("variables"))});
    if (!Inspector.Execute(Arguments, Result, Error) || !Result.IsValid()) return FString();
    const TArray<TSharedPtr<FJsonValue>>* Records = nullptr;
    if (!Result->TryGetArrayField(TEXT("records"), Records) || Records == nullptr) return FString();
    for (const TSharedPtr<FJsonValue>& Item : *Records)
    {
        const TSharedPtr<FJsonObject>* RecordObject = nullptr;
        FString RecordName;
        FString Id;
        if (Item.IsValid() && Item->TryGetObject(RecordObject) && RecordObject != nullptr && RecordObject->IsValid()
            && (*RecordObject)->TryGetStringField(TEXT("name"), RecordName) && RecordName == Name
            && (*RecordObject)->TryGetStringField(TEXT("id"), Id)) return Id;
    }
    return FString();
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
    const FString PackageName = TEXT("/Game/UnrealMCPPhase3/BP_CreationFixture_")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase4OperationLedgerTest, "UnrealMCP.Phase4.OperationLedger", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase4OperationLedgerTest::RunTest(const FString& Parameters)
{
    double CurrentTime = 10.0;
    const FString BridgeId = TEXT("0123456789abcdef0123456789abcdef");
    FUnrealMCPOperationLedger Ledger(BridgeId, TEXT("bounded-test-context"), [&CurrentTime] { return CurrentTime; });
    const FString OperationId = TEXT("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    const TSharedRef<FJsonObject> Arguments = MakeShared<FJsonObject>();
    Arguments->SetStringField(TEXT("operation_id"), OperationId);
    Arguments->SetStringField(TEXT("asset_path"), TEXT("/Game/BP_Test.BP_Test"));
    FUnrealMCPOperationAdmission Admission = Ledger.Admit(TEXT("blueprint_save"), Arguments);
    TestEqual(TEXT("new operation is accepted"), Admission.Kind, EUnrealMCPOperationAdmission::Accepted);
    FUnrealMCPError Error;
    TestTrue(TEXT("accepted operation starts executing"), Ledger.MarkExecuting(OperationId, Error));
    const TSharedRef<FJsonObject> Committed = MakeShared<FJsonObject>();
    Committed->SetStringField(TEXT("snapshot_id"), FString::ChrN(40, TEXT('b')));
    Ledger.Commit(OperationId, Committed);
    Admission = Ledger.Admit(TEXT("blueprint_save"), Arguments);
    TestEqual(TEXT("same request replays"), Admission.Kind, EUnrealMCPOperationAdmission::ReplaySuccess);
    TestTrue(TEXT("replay returns retained result"), Admission.Result == Committed);

    Arguments->SetStringField(TEXT("asset_path"), TEXT("/Game/BP_Other.BP_Other"));
    Admission = Ledger.Admit(TEXT("blueprint_save"), Arguments);
    TestEqual(TEXT("conflicting ID reuse rejects"), Admission.Kind, EUnrealMCPOperationAdmission::Conflict);
    TestEqual(TEXT("conflict code is stable"), Admission.Error->Code, FString(TEXT("operation_conflict")));

    const FString CancelId = TEXT("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    const TSharedRef<FJsonObject> Queued = MakeShared<FJsonObject>();
    Queued->SetStringField(TEXT("operation_id"), CancelId);
    Ledger.Admit(TEXT("blueprint_compile"), Queued);
    const TSharedRef<FJsonObject> StatusArguments = MakeShared<FJsonObject>();
    StatusArguments->SetStringField(TEXT("operation_id"), CancelId);
    StatusArguments->SetStringField(TEXT("bridge_instance_id"), BridgeId);
    StatusArguments->SetBoolField(TEXT("cancel"), true);
    TSharedPtr<FJsonObject> Status;
    TestTrue(TEXT("queued cancellation resolves"), Ledger.Status(StatusArguments, Status, Error));
    TestEqual(TEXT("queued operation becomes cancelled"), Status->GetStringField(TEXT("state")), FString(TEXT("cancelled")));
    TestFalse(TEXT("cancelled operation never executes"), Ledger.MarkExecuting(CancelId, Error));

    StatusArguments->SetStringField(TEXT("bridge_instance_id"), TEXT("cccccccccccccccccccccccccccccccc"));
    TestTrue(TEXT("another bridge instance resolves safely"), Ledger.Status(StatusArguments, Status, Error));
    TestEqual(TEXT("bridge restart returns unknown outcome"), Status->GetStringField(TEXT("state")), FString(TEXT("outcome_unknown")));
    CurrentTime += UnrealMCP::OperationLifetimeSeconds + 1.0;
    StatusArguments->SetStringField(TEXT("operation_id"), OperationId);
    StatusArguments->SetStringField(TEXT("bridge_instance_id"), BridgeId);
    StatusArguments->RemoveField(TEXT("cancel"));
    TestTrue(TEXT("expired result resolves safely"), Ledger.Status(StatusArguments, Status, Error));
    TestEqual(TEXT("expired result becomes unknown"), Status->GetStringField(TEXT("state")), FString(TEXT("outcome_unknown")));

    FUnrealMCPOperationLedger BoundedLedger(BridgeId, TEXT("bounded-capacity-context"), [] { return 20.0; });
    for (int32 Index = 0; Index < UnrealMCP::MaxRetainedOperations + 1; ++Index)
    {
        const FString Id = FString::Printf(TEXT("%032x"), Index + 1);
        const TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
        Request->SetStringField(TEXT("operation_id"), Id);
        FUnrealMCPOperationAdmission CapacityAdmission = BoundedLedger.Admit(TEXT("blueprint_save"), Request);
        TestEqual(TEXT("capacity admits by evicting the oldest terminal result"), CapacityAdmission.Kind, EUnrealMCPOperationAdmission::Accepted);
        TestTrue(TEXT("capacity fixture executes"), BoundedLedger.MarkExecuting(Id, Error));
        BoundedLedger.Commit(Id, MakeShared<FJsonObject>());
    }
    TestEqual(TEXT("ledger remains at its published bound"),
        BoundedLedger.CurrentState()->GetIntegerField(TEXT("retained")), UnrealMCP::MaxRetainedOperations);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase4PropertyCodecTest, "UnrealMCP.Phase4.PropertyCodec", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase4PropertyCodecTest::RunTest(const FString& Parameters)
{
    TSharedPtr<FJsonObject> Changed;
    FUnrealMCPError Error;
    UBlueprint* ReferenceBlueprint = CreateBlueprintFixture(
        TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_CodecClass"), AActor::StaticClass(), false);
    if (!TestNotNull(TEXT("Blueprint class reference fixture exists"), ReferenceBlueprint)) return false;
    UTextRenderComponent* Text = NewObject<UTextRenderComponent>();
    TestTrue(TEXT("Boolean form writes"), UnrealMCP::PropertyCodec::Set(Text, TEXT("bVisible"), MakeShared<FJsonValueBoolean>(false), Changed, Error));
    TestFalse(TEXT("Boolean form reads back exactly"), Changed->GetBoolField(TEXT("value")));
    TestTrue(TEXT("finite numeric form writes"), UnrealMCP::PropertyCodec::Set(Text, TEXT("WorldSize"), MakeShared<FJsonValueNumber>(42.5), Changed, Error));
    TestEqual(TEXT("finite numeric form reads back exactly"), Changed->GetNumberField(TEXT("value")), 42.5);
    TestTrue(TEXT("text form writes"), UnrealMCP::PropertyCodec::Set(Text, TEXT("Text"), MakeShared<FJsonValueString>(TEXT("Phase Four")), Changed, Error));
    TestEqual(TEXT("text form reads back exactly"), Changed->GetStringField(TEXT("value")), FString(TEXT("Phase Four")));
    TestTrue(TEXT("enum form writes"), UnrealMCP::PropertyCodec::Set(Text, TEXT("HorizontalAlignment"), MakeShared<FJsonValueString>(TEXT("EHTA_Center")), Changed, Error));
    TestTrue(TEXT("enum form is supported"), Changed->GetBoolField(TEXT("supported")));
    TestTrue(TEXT("safe struct form writes"), UnrealMCP::PropertyCodec::Set(Text, TEXT("TextRenderColor"),
        MakeShared<FJsonValueString>(TEXT("(R=10,G=20,B=30,A=255)")), Changed, Error));
    TestEqual(TEXT("safe struct form reads back canonically"), Changed->GetStringField(TEXT("value")), FString(TEXT("(B=30,G=20,R=10,A=255)")));

    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>();
    TestTrue(TEXT("string form writes"), UnrealMCP::PropertyCodec::Set(Capture, TEXT("ProfilingEventName"),
        MakeShared<FJsonValueString>(TEXT("UnrealMCP")), Changed, Error));
    TestEqual(TEXT("string form reads back exactly"), Changed->GetStringField(TEXT("value")), FString(TEXT("UnrealMCP")));
    TestTrue(TEXT("name form writes"), UnrealMCP::PropertyCodec::Set(Capture, TEXT("CollectionTransformWorldToLocal"),
        MakeShared<FJsonValueString>(TEXT("WorldToLocal")), Changed, Error));
    TestEqual(TEXT("name form reads back exactly"), Changed->GetStringField(TEXT("value")), FString(TEXT("WorldToLocal")));

    UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>();
    const TArray<TSharedPtr<FJsonValue>> Flags = {
        MakeShared<FJsonValueString>(TEXT("HLOD0")), MakeShared<FJsonValueString>(TEXT("HLOD2"))};
    const bool bFlagsWritten = UnrealMCP::PropertyCodec::Set(Mesh, TEXT("ExcludeFromHLODLevels"),
        MakeShared<FJsonValueArray>(Flags), Changed, Error);
    if (!bFlagsWritten) AddInfo(TEXT("flags form diagnostic: ") + Error.Code + TEXT(": ") + Error.Message);
    if (TestTrue(TEXT("flags form writes"), bFlagsWritten))
    {
        TestEqual(TEXT("flags form reads back both names"), Changed->GetArrayField(TEXT("value")).Num(), 2);
    }
    TestTrue(TEXT("visible engine asset reference writes"), UnrealMCP::PropertyCodec::Set(Mesh, TEXT("StaticMesh"),
        MakeShared<FJsonValueString>(TEXT("/Engine/BasicShapes/Cube.Cube")), Changed, Error));
    TestEqual(TEXT("hard asset reference reads back exactly"), Changed->GetStringField(TEXT("value")), FString(TEXT("/Engine/BasicShapes/Cube.Cube")));

    UVolumetricCloudComponent* Cloud = NewObject<UVolumetricCloudComponent>();
    TestTrue(TEXT("soft asset reference writes"), UnrealMCP::PropertyCodec::Set(Cloud, TEXT("Material"),
        MakeShared<FJsonValueString>(TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")), Changed, Error));
    TestEqual(TEXT("soft asset reference reads back exactly"), Changed->GetStringField(TEXT("value")), FString(TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")));

    UChildActorComponent* Child = NewObject<UChildActorComponent>();
    TestTrue(TEXT("Blueprint class reference writes"), UnrealMCP::PropertyCodec::Set(Child, TEXT("ChildActorClass"),
        MakeShared<FJsonValueString>(ReferenceBlueprint->GeneratedClass->GetPathName()), Changed, Error));
    TestEqual(TEXT("Blueprint class reference reads back exactly"), Changed->GetStringField(TEXT("value")), ReferenceBlueprint->GeneratedClass->GetPathName());

    UInputSettings* Input = NewObject<UInputSettings>();
    const bool bSoftClassWritten = UnrealMCP::PropertyCodec::Set(Input, TEXT("DefaultInputComponentClass"),
        MakeShared<FJsonValueString>(TEXT("/Script/Engine.InputComponent")), Changed, Error);
    if (!bSoftClassWritten) AddInfo(TEXT("soft class diagnostic: ") + Error.Code + TEXT(": ") + Error.Message);
    if (TestTrue(TEXT("soft native class reference writes"), bSoftClassWritten))
    {
        TestEqual(TEXT("soft class reference reads back exactly"), Changed->GetStringField(TEXT("value")), FString(TEXT("/Script/Engine.InputComponent")));
    }

    TestFalse(TEXT("incompatible object reference rejects"), UnrealMCP::PropertyCodec::Set(Mesh, TEXT("StaticMesh"),
        MakeShared<FJsonValueString>(TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial")), Changed, Error));
    TestFalse(TEXT("unsupported container rejects"), UnrealMCP::PropertyCodec::Set(Text, TEXT("ComponentTags"),
        MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>()), Changed, Error));
    TestEqual(TEXT("unsupported container error is stable"), Error.Code, FString(TEXT("unsupported_property")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase4ComponentAndDefaultTest, "UnrealMCP.Phase4.ComponentAndDefaultEdits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase4ComponentAndDefaultTest::RunTest(const FString& Parameters)
{
    const FString PackageName = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Phase4");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), false);
    if (!TestNotNull(TEXT("Phase 4 Blueprint fixture is created"), Blueprint)) return false;
    const FString AssetPath = Blueprint->GetPathName();
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    FString Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> AddRoot = ComponentEditArguments(AssetPath, Snapshot, TEXT("add"));
    AddRoot->SetStringField(TEXT("component_class"), USceneComponent::StaticClass()->GetPathName());
    AddRoot->SetStringField(TEXT("name"), TEXT("SceneRoot"));
    if (!TestTrue(TEXT("local scene component add succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), AddRoot, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString RootId = ComponentIdByName(Inspector, AssetPath, TEXT("SceneRoot"));
    TestEqual(TEXT("added component gets stable identity"), RootId.Len(), 32);

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> SetRoot = ComponentEditArguments(AssetPath, Snapshot, TEXT("set_root"));
    SetRoot->SetStringField(TEXT("component_id"), RootId);
    if (!TestTrue(TEXT("scene root replacement succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), SetRoot, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddMesh = ComponentEditArguments(AssetPath, Snapshot, TEXT("add"));
    AddMesh->SetStringField(TEXT("component_class"), UStaticMeshComponent::StaticClass()->GetPathName());
    AddMesh->SetStringField(TEXT("name"), TEXT("Mesh"));
    AddMesh->SetStringField(TEXT("parent_id"), RootId);
    if (!TestTrue(TEXT("attached scene component add succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), AddMesh, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    FString MeshId = ComponentIdByName(Inspector, AssetPath, TEXT("Mesh"));
    TestEqual(TEXT("attached component gets stable identity"), MeshId.Len(), 32);

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddMovement = ComponentEditArguments(AssetPath, Snapshot, TEXT("add"));
    AddMovement->SetStringField(TEXT("component_class"), URotatingMovementComponent::StaticClass()->GetPathName());
    AddMovement->SetStringField(TEXT("name"), TEXT("Movement"));
    if (!TestTrue(TEXT("non-scene component add succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), AddMovement, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> Rename = ComponentEditArguments(AssetPath, Snapshot, TEXT("rename"));
    Rename->SetStringField(TEXT("component_id"), MeshId);
    Rename->SetStringField(TEXT("new_name"), TEXT("Visual"));
    if (!TestTrue(TEXT("component rename succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), Rename, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("rename preserves stable identity"), ComponentIdByName(Inspector, AssetPath, TEXT("Visual")), MeshId);

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddPivot = ComponentEditArguments(AssetPath, Snapshot, TEXT("add"));
    AddPivot->SetStringField(TEXT("component_class"), USceneComponent::StaticClass()->GetPathName());
    AddPivot->SetStringField(TEXT("name"), TEXT("Pivot"));
    AddPivot->SetStringField(TEXT("parent_id"), RootId);
    if (!TestTrue(TEXT("second scene component add succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), AddPivot, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString PivotId = ComponentIdByName(Inspector, AssetPath, TEXT("Pivot"));

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> Reparent = ComponentEditArguments(AssetPath, Snapshot, TEXT("reparent"));
    Reparent->SetStringField(TEXT("component_id"), MeshId);
    Reparent->SetStringField(TEXT("new_parent_id"), PivotId);
    if (!TestTrue(TEXT("scene component reparent succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), Reparent, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> Cycle = ComponentEditArguments(AssetPath, Snapshot, TEXT("reparent"));
    Cycle->SetStringField(TEXT("component_id"), PivotId);
    Cycle->SetStringField(TEXT("new_parent_id"), MeshId);
    TestFalse(TEXT("attachment cycle rejects"), Mutator.Execute(TEXT("blueprint_component_edit"), Cycle, Result, Error));
    TestEqual(TEXT("cycle rejection is stable"), Error.Code, FString(TEXT("invalid_component")));
    TestEqual(TEXT("cycle rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> Duplicate = ComponentEditArguments(AssetPath, Snapshot, TEXT("add"));
    Duplicate->SetStringField(TEXT("component_class"), USceneComponent::StaticClass()->GetPathName());
    Duplicate->SetStringField(TEXT("name"), TEXT("Pivot"));
    TestFalse(TEXT("duplicate component name rejects"), Mutator.Execute(TEXT("blueprint_component_edit"), Duplicate, Result, Error));
    TestEqual(TEXT("duplicate-name rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> InvalidClass = ComponentEditArguments(AssetPath, Snapshot, TEXT("add"));
    InvalidClass->SetStringField(TEXT("component_class"), AActor::StaticClass()->GetPathName());
    InvalidClass->SetStringField(TEXT("name"), TEXT("Invalid"));
    TestFalse(TEXT("non-component class rejects"), Mutator.Execute(TEXT("blueprint_component_edit"), InvalidClass, Result, Error));
    TestEqual(TEXT("invalid class error is stable"), Error.Code, FString(TEXT("invalid_component")));

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> SetVisible = ComponentEditArguments(AssetPath, Snapshot, TEXT("set_property"));
    SetVisible->SetStringField(TEXT("component_id"), MeshId);
    SetVisible->SetStringField(TEXT("property_name"), TEXT("bVisible"));
    SetVisible->SetBoolField(TEXT("value"), false);
    if (!TestTrue(TEXT("component Boolean default edit succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), SetVisible, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestFalse(TEXT("component edit returns exact read-back"), Result->GetObjectField(TEXT("changed"))->GetBoolField(TEXT("value")));

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    const FString BeforeClassDefault = Snapshot;
    const TSharedRef<FJsonObject> SetActorDefault = MakeShared<FJsonObject>();
    SetActorDefault->SetStringField(TEXT("operation_id"), FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower());
    SetActorDefault->SetStringField(TEXT("asset_path"), AssetPath);
    SetActorDefault->SetStringField(TEXT("expected_snapshot"), Snapshot);
    SetActorDefault->SetStringField(TEXT("property_name"), TEXT("InitialLifeSpan"));
    SetActorDefault->SetNumberField(TEXT("value"), 12.5);
    if (!TestTrue(TEXT("Actor class default edit succeeds"), Mutator.Execute(TEXT("blueprint_default_edit"), SetActorDefault, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("class default round trips exactly"), Result->GetObjectField(TEXT("changed"))->GetNumberField(TEXT("value")), 12.5);
    const FString AfterClassDefault = Result->GetStringField(TEXT("snapshot_id"));
    TestTrue(TEXT("class-default transaction undoes"), GEditor != nullptr && GEditor->UndoTransaction());
    TestEqual(TEXT("Undo restores prior class-default snapshot"), InspectSnapshot(Inspector, AssetPath), BeforeClassDefault);
    TestTrue(TEXT("class-default transaction redoes"), GEditor != nullptr && GEditor->RedoTransaction());
    TestEqual(TEXT("Redo restores edited class-default snapshot"), InspectSnapshot(Inspector, AssetPath), AfterClassDefault);

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    const TSharedRef<FJsonObject> TargetedInspect = InspectArguments(AssetPath);
    TargetedInspect->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("components")), MakeShared<FJsonValueString>(TEXT("class_defaults"))});
    TargetedInspect->SetStringField(TEXT("component_id"), MeshId);
    TargetedInspect->SetArrayField(TEXT("property_names"), {MakeShared<FJsonValueString>(TEXT("bVisible")), MakeShared<FJsonValueString>(TEXT("InitialLifeSpan"))});
    if (!TestTrue(TEXT("targeted component and class-default inspection succeeds"), Inspector.Execute(TargetedInspect, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestTrue(TEXT("targeted component record is present"), ResultHasSection(Result, TEXT("component")));
    TestTrue(TEXT("targeted class-default record is present"), ResultHasSection(Result, TEXT("class_default")));

    TSharedRef<FJsonObject> Stale = ComponentEditArguments(AssetPath, FString::ChrN(40, TEXT('0')), TEXT("remove"));
    Stale->SetStringField(TEXT("component_id"), MeshId);
    TestFalse(TEXT("stale snapshot rejects before mutation"), Mutator.Execute(TEXT("blueprint_component_edit"), Stale, Result, Error));
    TestEqual(TEXT("stale error is stable"), Error.Code, FString(TEXT("stale_precondition")));
    TestEqual(TEXT("rejection preserves structure"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> RemoveVisual = ComponentEditArguments(AssetPath, Snapshot, TEXT("remove"));
    RemoveVisual->SetStringField(TEXT("component_id"), MeshId);
    if (!TestTrue(TEXT("leaf component removal succeeds"), Mutator.Execute(TEXT("blueprint_component_edit"), RemoveVisual, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestTrue(TEXT("removed component identity is unavailable"), ComponentIdByName(Inspector, AssetPath, TEXT("Visual")).IsEmpty());

    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
    TestEqual(TEXT("edited Blueprint compiles without errors"), Log.NumErrors, 0);
    TestTrue(TEXT("edited Blueprint saves"), SaveBlueprintFixture(Blueprint));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase5K2TypeCodecTest, "UnrealMCP.Phase5.K2TypeCodec", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase5K2TypeCodecTest::RunTest(const FString& Parameters)
{
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUnrealMCPPhase6FunctionAndLocalTest, "UnrealMCP.Phase6.FunctionsAndLocals", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FUnrealMCPPhase6FunctionAndLocalTest::RunTest(const FString& Parameters)
{
    const FString PackageName = TEXT("/Game/UnrealMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Phase6");
    UBlueprint* Blueprint = CreateBlueprintFixture(PackageName, AActor::StaticClass(), false);
    if (!TestNotNull(TEXT("Phase 6 Blueprint fixture is created"), Blueprint)) return false;
    const FString AssetPath = Blueprint->GetPathName();
    FUnrealMCPBlueprintInspector Inspector;
    FUnrealMCPBlueprintMutator Mutator(Inspector);
    TSharedPtr<FJsonObject> Result;
    FUnrealMCPError Error;

    FString Snapshot = InspectSnapshot(Inspector, AssetPath);
    TSharedRef<FJsonObject> IntType = K2Type(TEXT("int"));
    TSharedRef<FJsonObject> ConstRefString = K2Type(TEXT("string"));
    ConstRefString->SetBoolField(TEXT("reference"), true);
    ConstRefString->SetBoolField(TEXT("const"), true);
    TArray<TSharedPtr<FJsonValue>> ParametersJson = {
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Count"), TEXT("input"), IntType,
            LiteralDefault(MakeShared<FJsonValueNumber>(3)))),
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Label"), TEXT("input"), ConstRefString)),
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Succeeded"), TEXT("output"), K2Type(TEXT("boolean"))))};
    TSharedRef<FJsonObject> AddFunction = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("add"));
    AddFunction->SetStringField(TEXT("name"), TEXT("Compute"));
    AddFunction->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("protected"), false, true, ParametersJson));
    const TSharedRef<FJsonObject> FunctionMetadata = MakeShared<FJsonObject>();
    FunctionMetadata->SetStringField(TEXT("category"), TEXT("Unreal MCP"));
    FunctionMetadata->SetStringField(TEXT("tooltip"), TEXT("Computes one bounded result"));
    FunctionMetadata->SetStringField(TEXT("keywords"), TEXT("compute bounded"));
    AddFunction->SetObjectField(TEXT("metadata"), FunctionMetadata);
    if (!TestTrue(TEXT("function shell and complete signature add succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddFunction, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString FunctionId = Result->GetObjectField(TEXT("function"))->GetStringField(TEXT("id"));
    TestEqual(TEXT("function gets stable graph identity"), FunctionId.Len(), 32);
    TestEqual(TEXT("function signature reads all parameter directions"),
        Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("signature"))->GetArrayField(TEXT("parameters")).Num(), 3);
    const TArray<TSharedPtr<FJsonValue>>& ReadParameters =
        Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("signature"))->GetArrayField(TEXT("parameters"));
    TestTrue(TEXT("ordinary input retains its tagged default"), ReadParameters[0]->AsObject()->HasField(TEXT("default")));
    TestFalse(TEXT("reference input does not invent a default"), ReadParameters[1]->AsObject()->HasField(TEXT("default")));
    TestFalse(TEXT("output does not invent a default"), ReadParameters[2]->AsObject()->HasField(TEXT("default")));
    TestTrue(TEXT("function preserves required entry and result nodes"),
        Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("required_nodes"))->GetBoolField(TEXT("valid")));
    TestEqual(TEXT("function metadata reads back exactly"),
        Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("metadata"))->GetStringField(TEXT("category")), FString(TEXT("Unreal MCP")));

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    const bool bDirtyBeforeInvalidSignature = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBeforeInvalidSignature = Blueprint->Status;
    const int32 TransactionsBeforeInvalidSignature = GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0;
    TSharedRef<FJsonObject> OutputReference = K2Type(TEXT("boolean"));
    OutputReference->SetBoolField(TEXT("reference"), true);
    TSharedRef<FJsonObject> InvalidOutputSignature = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("update"));
    InvalidOutputSignature->SetStringField(TEXT("function_id"), FunctionId);
    InvalidOutputSignature->SetStringField(TEXT("field"), TEXT("signature"));
    InvalidOutputSignature->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    InvalidOutputSignature->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("public"), false, false, {
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Invalid"), TEXT("output"), OutputReference))}));
    TestFalse(TEXT("unsupported output-reference signature rejects before mutation"),
        Mutator.Execute(TEXT("blueprint_member_edit"), InvalidOutputSignature, Result, Error));
    TestEqual(TEXT("invalid signature preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);
    TestEqual(TEXT("invalid signature preserves package dirty state"), Blueprint->GetOutermost()->IsDirty(), bDirtyBeforeInvalidSignature);
    TestEqual(TEXT("invalid signature preserves compile state"), Blueprint->Status, StatusBeforeInvalidSignature);
    TestEqual(TEXT("invalid signature creates no transaction"),
        GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0, TransactionsBeforeInvalidSignature);

    TSharedRef<FJsonObject> RenameFunction = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("rename"));
    RenameFunction->SetStringField(TEXT("function_id"), FunctionId);
    RenameFunction->SetStringField(TEXT("new_name"), TEXT("ComputeHealth"));
    if (!TestTrue(TEXT("function rename succeeds and preserves graph identity"),
        Mutator.Execute(TEXT("blueprint_member_edit"), RenameFunction, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("function rename preserves stable identity"),
        ScopedIdByName(Inspector, AssetPath, TEXT("functions"), TEXT("ComputeHealth")), FunctionId);

    TSharedRef<FJsonObject> UpdateSignature = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("function"), TEXT("update"));
    UpdateSignature->SetStringField(TEXT("function_id"), FunctionId);
    UpdateSignature->SetStringField(TEXT("field"), TEXT("signature"));
    UpdateSignature->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    UpdateSignature->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("public"), true, false, ParametersJson));
    if (!TestTrue(TEXT("unreferenced complete-signature update succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateSignature, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const TSharedPtr<FJsonObject> UpdatedSignature = Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("signature"));
    TestEqual(TEXT("signature access updates exactly"), UpdatedSignature->GetStringField(TEXT("access")), FString(TEXT("public")));
    TestTrue(TEXT("signature pure flag updates exactly"), UpdatedSignature->GetBoolField(TEXT("pure")));
    TestFalse(TEXT("signature const flag updates exactly"), UpdatedSignature->GetBoolField(TEXT("const")));

    TSharedRef<FJsonObject> UpdateFunctionMetadata = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("function"), TEXT("update"));
    UpdateFunctionMetadata->SetStringField(TEXT("function_id"), FunctionId);
    UpdateFunctionMetadata->SetStringField(TEXT("field"), TEXT("metadata"));
    const TSharedRef<FJsonObject> UpdatedMetadata = MakeShared<FJsonObject>();
    UpdatedMetadata->SetStringField(TEXT("category"), TEXT("Utilities"));
    UpdatedMetadata->SetBoolField(TEXT("call_in_editor"), true);
    UpdateFunctionMetadata->SetObjectField(TEXT("metadata"), UpdatedMetadata);
    if (!TestTrue(TEXT("function metadata update succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateFunctionMetadata, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("function metadata category updates exactly"),
        Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("metadata"))->GetStringField(TEXT("category")), FString(TEXT("Utilities")));
    TestTrue(TEXT("function call-in-editor metadata updates exactly"),
        Result->GetObjectField(TEXT("function"))->GetObjectField(TEXT("metadata"))->GetBoolField(TEXT("call_in_editor")));

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    const int32 TransactionsBeforeCollision = GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0;
    TSharedRef<FJsonObject> CollidingLocal = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("local_variable"), TEXT("add"));
    CollidingLocal->SetStringField(TEXT("function_id"), FunctionId);
    CollidingLocal->SetStringField(TEXT("name"), TEXT("Count"));
    CollidingLocal->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
    TestFalse(TEXT("local collision with a function parameter rejects"),
        Mutator.Execute(TEXT("blueprint_member_edit"), CollidingLocal, Result, Error));
    TestEqual(TEXT("local collision preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);
    TestEqual(TEXT("local collision creates no transaction"),
        GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0, TransactionsBeforeCollision);

    TSharedRef<FJsonObject> AddLocal = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("local_variable"), TEXT("add"));
    AddLocal->SetStringField(TEXT("function_id"), FunctionId);
    AddLocal->SetStringField(TEXT("name"), TEXT("Accumulator"));
    AddLocal->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
    AddLocal->SetObjectField(TEXT("default"), LiteralDefault(MakeShared<FJsonValueNumber>(9)));
    if (!TestTrue(TEXT("typed function-local variable add succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddLocal, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString LocalId = Result->GetObjectField(TEXT("local_variable"))->GetStringField(TEXT("id"));
    TestEqual(TEXT("local variable gets stable identity"), LocalId.Len(), 32);
    TestEqual(TEXT("local scope reports owning function"),
        Result->GetObjectField(TEXT("local_variable"))->GetObjectField(TEXT("scope"))->GetStringField(TEXT("function_id")), FunctionId);
    TestEqual(TEXT("local default reads back exactly"),
        Result->GetObjectField(TEXT("local_variable"))->GetObjectField(TEXT("default"))->GetNumberField(TEXT("value")), 9.0);

    const FString BeforeRename = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> RenameLocal = ScopedMemberEditArguments(AssetPath, BeforeRename, TEXT("local_variable"), TEXT("rename"));
    RenameLocal->SetStringField(TEXT("function_id"), FunctionId);
    RenameLocal->SetStringField(TEXT("local_id"), LocalId);
    RenameLocal->SetStringField(TEXT("new_name"), TEXT("RunningTotal"));
    if (!TestTrue(TEXT("local rename succeeds and preserves identity"),
        Mutator.Execute(TEXT("blueprint_member_edit"), RenameLocal, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString AfterRename = Result->GetStringField(TEXT("snapshot_id"));
    TestEqual(TEXT("local rename preserves stable identity"), ScopedIdByName(Inspector, AssetPath, TEXT("local_variables"), TEXT("RunningTotal")), LocalId);
    TestTrue(TEXT("local transaction undoes"), GEditor != nullptr && GEditor->UndoTransaction());
    TestEqual(TEXT("local undo restores snapshot"), InspectSnapshot(Inspector, AssetPath), BeforeRename);
    TestTrue(TEXT("local transaction redoes"), GEditor != nullptr && GEditor->RedoTransaction());
    TestEqual(TEXT("local redo restores snapshot"), InspectSnapshot(Inspector, AssetPath), AfterRename);

    Snapshot = AfterRename;
    TSharedRef<FJsonObject> UpdateLocalType = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("local_variable"), TEXT("update"));
    UpdateLocalType->SetStringField(TEXT("function_id"), FunctionId);
    UpdateLocalType->SetStringField(TEXT("local_id"), LocalId);
    UpdateLocalType->SetStringField(TEXT("field"), TEXT("type"));
    UpdateLocalType->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    UpdateLocalType->SetObjectField(TEXT("type"), K2Type(TEXT("string")));
    if (!TestTrue(TEXT("unreferenced local type update succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateLocalType, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("local type reads back exactly"),
        Result->GetObjectField(TEXT("local_variable"))->GetObjectField(TEXT("type"))->GetStringField(TEXT("category")), FString(TEXT("string")));

    TSharedRef<FJsonObject> UpdateLocalDefault = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("local_variable"), TEXT("update"));
    UpdateLocalDefault->SetStringField(TEXT("function_id"), FunctionId);
    UpdateLocalDefault->SetStringField(TEXT("local_id"), LocalId);
    UpdateLocalDefault->SetStringField(TEXT("field"), TEXT("default"));
    UpdateLocalDefault->SetObjectField(TEXT("default"), LiteralDefault(MakeShared<FJsonValueString>(TEXT("ready"))));
    if (!TestTrue(TEXT("local tagged-default update succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateLocalDefault, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestEqual(TEXT("local default reads back after update"),
        Result->GetObjectField(TEXT("local_variable"))->GetObjectField(TEXT("default"))->GetStringField(TEXT("value")), FString(TEXT("ready")));

    TSharedRef<FJsonObject> AddTemporaryLocal = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("local_variable"), TEXT("add"));
    AddTemporaryLocal->SetStringField(TEXT("function_id"), FunctionId);
    AddTemporaryLocal->SetStringField(TEXT("name"), TEXT("Scratch"));
    AddTemporaryLocal->SetObjectField(TEXT("type"), K2Type(TEXT("boolean")));
    if (!TestTrue(TEXT("second scoped local add succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), AddTemporaryLocal, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString TemporaryLocalId = Result->GetObjectField(TEXT("local_variable"))->GetStringField(TEXT("id"));
    TSharedRef<FJsonObject> RemoveTemporaryLocal = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("local_variable"), TEXT("remove"));
    RemoveTemporaryLocal->SetStringField(TEXT("function_id"), FunctionId);
    RemoveTemporaryLocal->SetStringField(TEXT("local_id"), TemporaryLocalId);
    RemoveTemporaryLocal->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    if (!TestTrue(TEXT("unreferenced scoped local removal succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), RemoveTemporaryLocal, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddTemporary = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("add"));
    AddTemporary->SetStringField(TEXT("name"), TEXT("Temporary"));
    AddTemporary->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("private"), true, false, {}));
    if (!TestTrue(TEXT("temporary pure function add succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), AddTemporary, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString TemporaryId = Result->GetObjectField(TEXT("function"))->GetStringField(TEXT("id"));
    TSharedRef<FJsonObject> RemoveTemporary = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("function"), TEXT("remove"));
    RemoveTemporary->SetStringField(TEXT("function_id"), TemporaryId);
    RemoveTemporary->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    if (!TestTrue(TEXT("unreferenced function removal succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), RemoveTemporary, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> AddNotify = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("add"));
    AddNotify->SetStringField(TEXT("name"), TEXT("OnRep_Health"));
    AddNotify->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("private"), false, false, {}));
    if (!TestTrue(TEXT("RepNotify-compatible function add succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), AddNotify, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    const FString NotifyId = Result->GetObjectField(TEXT("function"))->GetStringField(TEXT("id"));

    TSharedRef<FJsonObject> AddHealth = MemberEditArguments(AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("add"));
    AddHealth->SetStringField(TEXT("name"), TEXT("Health"));
    AddHealth->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
    const TSharedRef<FJsonObject> RepNotifyMetadata = MakeShared<FJsonObject>();
    RepNotifyMetadata->SetStringField(TEXT("replication"), TEXT("rep_notify"));
    RepNotifyMetadata->SetStringField(TEXT("rep_notify_function"), TEXT("OnRep_Health"));
    RepNotifyMetadata->SetStringField(TEXT("replication_condition"), TEXT("COND_OwnerOnly"));
    AddHealth->SetObjectField(TEXT("metadata"), RepNotifyMetadata);
    if (!TestTrue(TEXT("RepNotify member coupling succeeds"), Mutator.Execute(TEXT("blueprint_member_edit"), AddHealth, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TestTrue(TEXT("RepNotify relationship reads as valid"),
        Result->GetObjectField(TEXT("member"))->GetObjectField(TEXT("replication"))->GetBoolField(TEXT("relationship_valid")));
    TestEqual(TEXT("RepNotify relationship carries stable function identity"),
        Result->GetObjectField(TEXT("member"))->GetObjectField(TEXT("replication"))->GetStringField(TEXT("rep_notify_function_id")), NotifyId);
    const FString HealthId = Result->GetObjectField(TEXT("member"))->GetStringField(TEXT("id"));

    TSharedRef<FJsonObject> RenameNotify = ScopedMemberEditArguments(
        AssetPath, Result->GetStringField(TEXT("snapshot_id")), TEXT("function"), TEXT("rename"));
    RenameNotify->SetStringField(TEXT("function_id"), NotifyId);
    RenameNotify->SetStringField(TEXT("new_name"), TEXT("OnRep_CurrentHealth"));
    if (!TestTrue(TEXT("RepNotify function rename succeeds"),
        Mutator.Execute(TEXT("blueprint_member_edit"), RenameNotify, Result, Error)))
    { AddError(Error.Code + TEXT(": ") + Error.Message); return false; }
    TSharedPtr<FJsonObject> RepNotifyInspection;
    const TSharedRef<FJsonObject> InspectHealth = InspectArguments(AssetPath);
    InspectHealth->SetStringField(TEXT("member_id"), HealthId);
    InspectHealth->SetArrayField(TEXT("sections"), {MakeShared<FJsonValueString>(TEXT("variables"))});
    if (!TestTrue(TEXT("renamed RepNotify relationship remains inspectable"),
        Inspector.Execute(InspectHealth, RepNotifyInspection, Error)) || !RepNotifyInspection.IsValid()) return false;
    const TSharedPtr<FJsonObject> RenamedHealth = RepNotifyInspection->GetArrayField(TEXT("records"))[0]->AsObject();
    TestEqual(TEXT("RepNotify rename updates member relationship"),
        RenamedHealth->GetObjectField(TEXT("replication"))->GetStringField(TEXT("rep_notify_function")), FString(TEXT("OnRep_CurrentHealth")));
    TestEqual(TEXT("RepNotify rename preserves function identity"),
        RenamedHealth->GetObjectField(TEXT("replication"))->GetStringField(TEXT("rep_notify_function_id")), NotifyId);

    Snapshot = Result->GetStringField(TEXT("snapshot_id"));
    TSharedRef<FJsonObject> InvalidNotifySignature = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("update"));
    InvalidNotifySignature->SetStringField(TEXT("function_id"), NotifyId);
    InvalidNotifySignature->SetStringField(TEXT("field"), TEXT("signature"));
    InvalidNotifySignature->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    InvalidNotifySignature->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("private"), false, false, {
        MakeShared<FJsonValueObject>(FunctionParameter(TEXT("Invalid"), TEXT("input"), K2Type(TEXT("int"))))}));
    TestFalse(TEXT("invalid RepNotify signature change rejects"),
        Mutator.Execute(TEXT("blueprint_member_edit"), InvalidNotifySignature, Result, Error));
    TestEqual(TEXT("RepNotify signature rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    FCompilerResultsLog Log;
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Log);
    if (!TestEqual(TEXT("Phase 6 Blueprint compiles without errors"), Log.NumErrors, 0)) return false;
    UEdGraph* FunctionGraph = nullptr;
    for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
        if (Candidate != nullptr && Candidate->GraphGuid.ToString(EGuidFormats::Digits).ToLower() == FunctionId) FunctionGraph = Candidate;
    UK2Node_FunctionEntry* FunctionEntry = FunctionGraph != nullptr ? Cast<UK2Node_FunctionEntry>(FBlueprintEditorUtils::GetEntryNode(FunctionGraph)) : nullptr;
    FBPVariableDescription* Local = nullptr;
    if (FunctionEntry != nullptr)
    {
        for (FBPVariableDescription& Candidate : FunctionEntry->LocalVariables)
            if (Candidate.VarGuid.ToString(EGuidFormats::Digits).ToLower() == LocalId) Local = &Candidate;
    }
    if (!TestNotNull(TEXT("function graph survives compilation"), FunctionGraph)
        || !TestNotNull(TEXT("local variable survives compilation"), Local)) return false;
    UK2Node_VariableGet* LocalGetter = NewObject<UK2Node_VariableGet>(FunctionGraph);
    LocalGetter->VariableReference.SetLocalMember(Local->VarName, FunctionGraph->GetName(), Local->VarGuid);
    LocalGetter->CreateNewGuid();
    FunctionGraph->AddNode(LocalGetter, true, false);
    LocalGetter->PostPlacedNewNode();
    LocalGetter->AllocateDefaultPins();
    UFunction* GeneratedFunction = Blueprint->SkeletonGeneratedClass != nullptr
        ? Blueprint->SkeletonGeneratedClass->FindFunctionByName(FunctionGraph->GetFName()) : nullptr;
    UEdGraph* EventGraph = !Blueprint->UbergraphPages.IsEmpty() ? Blueprint->UbergraphPages[0] : nullptr;
    if (!TestNotNull(TEXT("generated function exists for call reference"), GeneratedFunction)
        || !TestNotNull(TEXT("event graph exists for call reference"), EventGraph)) return false;
    UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(EventGraph);
    Call->SetFromFunction(GeneratedFunction);
    Call->CreateNewGuid();
    EventGraph->AddNode(Call, true, false);
    Call->PostPlacedNewNode();
    Call->AllocateDefaultPins();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    Snapshot = InspectSnapshot(Inspector, AssetPath);
    const bool bDirtyBeforeReferenceRejections = Blueprint->GetOutermost()->IsDirty();
    const EBlueprintStatus StatusBeforeReferenceRejections = Blueprint->Status;
    const int32 TransactionsBeforeReferenceRejections = GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0;

    TSharedRef<FJsonObject> UpdateUsedFunction = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("update"));
    UpdateUsedFunction->SetStringField(TEXT("function_id"), FunctionId);
    UpdateUsedFunction->SetStringField(TEXT("field"), TEXT("signature"));
    UpdateUsedFunction->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    UpdateUsedFunction->SetObjectField(TEXT("signature"), FunctionSignature(TEXT("private"), false, false, {}));
    TestFalse(TEXT("referenced function signature update rejects"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateUsedFunction, Result, Error));
    TestEqual(TEXT("referenced signature rejection uses stable error"), Error.Code, FString(TEXT("referenced_member")));
    TestEqual(TEXT("referenced signature rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> UpdateUsedLocal = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("local_variable"), TEXT("update"));
    UpdateUsedLocal->SetStringField(TEXT("function_id"), FunctionId);
    UpdateUsedLocal->SetStringField(TEXT("local_id"), LocalId);
    UpdateUsedLocal->SetStringField(TEXT("field"), TEXT("type"));
    UpdateUsedLocal->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    UpdateUsedLocal->SetObjectField(TEXT("type"), K2Type(TEXT("int")));
    TestFalse(TEXT("referenced local type update rejects"),
        Mutator.Execute(TEXT("blueprint_member_edit"), UpdateUsedLocal, Result, Error));
    TestEqual(TEXT("referenced local type rejection uses stable error"), Error.Code, FString(TEXT("referenced_member")));
    TestEqual(TEXT("referenced local type rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> RemoveUsedFunction = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("function"), TEXT("remove"));
    RemoveUsedFunction->SetStringField(TEXT("function_id"), FunctionId);
    RemoveUsedFunction->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    TestFalse(TEXT("referenced function removal rejects"), Mutator.Execute(TEXT("blueprint_member_edit"), RemoveUsedFunction, Result, Error));
    TestEqual(TEXT("referenced function rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);

    TSharedRef<FJsonObject> RemoveUsedLocal = ScopedMemberEditArguments(AssetPath, Snapshot, TEXT("local_variable"), TEXT("remove"));
    RemoveUsedLocal->SetStringField(TEXT("function_id"), FunctionId);
    RemoveUsedLocal->SetStringField(TEXT("local_id"), LocalId);
    RemoveUsedLocal->SetStringField(TEXT("policy"), TEXT("reject_if_referenced"));
    TestFalse(TEXT("referenced local removal rejects"), Mutator.Execute(TEXT("blueprint_member_edit"), RemoveUsedLocal, Result, Error));
    TestEqual(TEXT("referenced local rejection preserves snapshot"), InspectSnapshot(Inspector, AssetPath), Snapshot);
    TestEqual(TEXT("reference rejections preserve package dirty state"), Blueprint->GetOutermost()->IsDirty(), bDirtyBeforeReferenceRejections);
    TestEqual(TEXT("reference rejections preserve compile state"), Blueprint->Status, StatusBeforeReferenceRejections);
    TestEqual(TEXT("reference rejections create no transactions"),
        GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0, TransactionsBeforeReferenceRejections);

    TestTrue(TEXT("Phase 6 Blueprint saves"), SaveBlueprintFixture(Blueprint));
    return true;
}

#endif
