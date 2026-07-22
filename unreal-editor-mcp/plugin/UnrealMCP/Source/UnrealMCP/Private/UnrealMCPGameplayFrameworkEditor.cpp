#include "UnrealMCPGameplayFrameworkEditor.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/GameInstance.h"
#include "GameMapsSettings.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
const TCHAR* SettingsSection = TEXT("/Script/EngineSettings.GameMapsSettings");
constexpr int64 MaxConfigBytes = 4 * 1024 * 1024;

bool HasOnlyFields(const FJsonObject& Object)
{
    const TSet<FString> Allowed = {TEXT("operation_id"), TEXT("project_hash"), TEXT("setting"),
        TEXT("class_path"), TEXT("expected_class")};
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
    {
        if (!Allowed.Contains(Pair.Key)) return false;
    }
    return true;
}

FString CurrentClass(const FString& Setting)
{
    const UGameMapsSettings* Settings = GetDefault<UGameMapsSettings>();
    return Setting == TEXT("default_game_mode") ? UGameMapsSettings::GetGlobalDefaultGameMode()
        : Settings != nullptr ? Settings->GameInstanceClass.ToString() : FString();
}

bool ResolveClass(const FString& Setting, const FString& ClassPath, UClass*& OutClass, FUnrealMCPError& OutError)
{
    if (!ClassPath.StartsWith(TEXT("/")) || ClassPath.Contains(TEXT("..")) || ClassPath.Contains(TEXT("\\"))
        || ClassPath.Len() > 512 || (OutClass = LoadObject<UClass>(nullptr, *ClassPath, nullptr, LOAD_NoWarn | LOAD_Quiet)) == nullptr
        || OutClass->GetPathName() != ClassPath || OutClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
        || IsEditorOnlyObject(OutClass))
    {
        OutError = {TEXT("invalid_parent"), TEXT("class_path must resolve exactly to one usable native or saved Blueprint class")};
        return false;
    }
    const UClass* Required = Setting == TEXT("default_game_mode") ? AGameModeBase::StaticClass() : UGameInstance::StaticClass();
    if (!OutClass->IsChildOf(Required))
    {
        OutError = {TEXT("invalid_parent"), TEXT("The selected class is incompatible with the requested gameplay-framework setting")};
        return false;
    }
    if (UBlueprint* Blueprint = Cast<UBlueprint>(OutClass->ClassGeneratedBy))
    {
        const FString PackageName = Blueprint->GetOutermost()->GetName();
        if (Blueprint->GeneratedClass != OutClass || Blueprint->SkeletonGeneratedClass == OutClass
            || Blueprint->Status == BS_Error || Blueprint->GetOutermost()->IsDirty()
            || !FPackageName::DoesPackageExist(PackageName))
        {
            OutError = {TEXT("invalid_parent"), TEXT("Blueprint framework classes must be compiled, saved, and non-transient before assignment")};
            return false;
        }
    }
    return true;
}

FString UpdateIni(const FString& Original, const FString& Key, const FString& Value)
{
    TArray<FString> Lines;
    Original.ParseIntoArrayLines(Lines, false);
    int32 SectionStart = INDEX_NONE;
    int32 SectionEnd = Lines.Num();
    for (int32 Index = 0; Index < Lines.Num(); ++Index)
    {
        const FString Trimmed = Lines[Index].TrimStartAndEnd();
        if (Trimmed.StartsWith(TEXT("[")) && Trimmed.EndsWith(TEXT("]")))
        {
            if (SectionStart != INDEX_NONE) { SectionEnd = Index; break; }
            if (Trimmed.Mid(1, Trimmed.Len() - 2).Equals(SettingsSection, ESearchCase::IgnoreCase)) SectionStart = Index;
        }
    }
    if (SectionStart == INDEX_NONE)
    {
        if (!Lines.IsEmpty() && !Lines.Last().IsEmpty()) Lines.Add(FString());
        Lines.Add(FString::Printf(TEXT("[%s]"), SettingsSection));
        Lines.Add(Key + TEXT("=") + Value);
    }
    else
    {
        int32 InsertAt = SectionEnd;
        for (int32 Index = SectionEnd - 1; Index > SectionStart; --Index)
        {
            FString Left;
            FString Right;
            if (Lines[Index].Split(TEXT("="), &Left, &Right) && Left.TrimStartAndEnd().Equals(Key, ESearchCase::IgnoreCase))
            {
                InsertAt = Index;
                Lines.RemoveAt(Index);
            }
        }
        Lines.Insert(Key + TEXT("=") + Value, InsertAt);
    }
    return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
}

bool ReadIniValue(const FString& Content, const FString& Key, FString& Out)
{
    TArray<FString> Lines;
    Content.ParseIntoArrayLines(Lines, false);
    bool bInSection = false;
    bool bFound = false;
    for (const FString& Line : Lines)
    {
        const FString Trimmed = Line.TrimStartAndEnd();
        if (Trimmed.StartsWith(TEXT("[")) && Trimmed.EndsWith(TEXT("]")))
        {
            bInSection = Trimmed.Mid(1, Trimmed.Len() - 2).Equals(SettingsSection, ESearchCase::IgnoreCase);
            continue;
        }
        FString Left;
        FString Right;
        if (bInSection && !Trimmed.StartsWith(TEXT(";")) && !Trimmed.StartsWith(TEXT("#"))
            && Line.Split(TEXT("="), &Left, &Right) && Left.TrimStartAndEnd().Equals(Key, ESearchCase::IgnoreCase))
        {
            Out = Right.TrimStartAndEnd();
            bFound = true;
        }
    }
    return bFound;
}

bool Persist(const FString& Key, const FString& Value, FUnrealMCPError& OutError)
{
    IFileManager& Files = IFileManager::Get();
    const FString ConfigPath = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultEngine.ini"));
    const bool bExisted = Files.FileExists(*ConfigPath);
    if ((bExisted && Files.IsReadOnly(*ConfigPath)) || !Files.DirectoryExists(*FPaths::ProjectConfigDir()))
    {
        OutError = {TEXT("write_conflict"), TEXT("The project gameplay settings file is read-only or its directory is unavailable")};
        return false;
    }
    if (bExisted && Files.FileSize(*ConfigPath) > MaxConfigBytes)
    {
        OutError = {TEXT("response_too_large"), TEXT("The project gameplay settings file exceeds the bounded edit size")};
        return false;
    }
    FString Original;
    if (bExisted && !FFileHelper::LoadFileToString(Original, *ConfigPath))
    {
        OutError = {TEXT("write_conflict"), TEXT("The existing project gameplay settings could not be read")};
        return false;
    }
    const FString Updated = UpdateIni(Original, Key, Value);
    const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
    const FString Temporary = ConfigPath + TEXT(".unrealmcp.") + Suffix + TEXT(".tmp");
    if (!FFileHelper::SaveStringToFile(Updated, *Temporary, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        || !Files.Move(*ConfigPath, *Temporary, true, false, false, true))
    {
        Files.Delete(*Temporary, false, false, true);
        OutError = {TEXT("save_failed"), TEXT("The gameplay-framework assignment could not be persisted atomically")};
        return false;
    }
    FString ReadBack;
    FString Persisted;
    if (FFileHelper::LoadFileToString(ReadBack, *ConfigPath) && ReadIniValue(ReadBack, Key, Persisted) && Persisted == Value)
    {
        return true;
    }
    const FString Restore = ConfigPath + TEXT(".unrealmcp.") + Suffix + TEXT(".restore");
    const bool bRestored = bExisted
        ? FFileHelper::SaveStringToFile(Original, *Restore, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            && Files.Move(*ConfigPath, *Restore, true, false, false, true)
        : Files.Delete(*ConfigPath, false, false, true);
    Files.Delete(*Restore, false, false, true);
    OutError = {TEXT("save_failed"), bRestored
        ? TEXT("Gameplay settings read-back failed and the prior file was restored")
        : TEXT("Gameplay settings read-back failed and exact restoration also failed")};
    return false;
}
}

bool FUnrealMCPGameplayFrameworkEditor::Execute(
    const TSharedPtr<FJsonObject>& Arguments,
    TSharedPtr<FJsonObject>& OutResult,
    FUnrealMCPError& OutError)
{
    if (!Arguments.IsValid() || !HasOnlyFields(*Arguments))
    {
        OutError = {TEXT("invalid_argument"), TEXT("gameplay_framework_edit accepts only its exact typed assignment fields")};
        return false;
    }
    FString RequestProject;
    FString Setting;
    FString ClassPath;
    FString Expected;
    if (!Arguments->TryGetStringField(TEXT("project_hash"), RequestProject) || RequestProject != ProjectHash
        || !Arguments->TryGetStringField(TEXT("setting"), Setting)
        || (Setting != TEXT("default_game_mode") && Setting != TEXT("default_game_instance"))
        || !Arguments->TryGetStringField(TEXT("class_path"), ClassPath)
        || !Arguments->TryGetStringField(TEXT("expected_class"), Expected))
    {
        OutError = {TEXT("invalid_argument"), TEXT("The assignment must name this exact project, one supported setting, class, and expected current class")};
        return false;
    }
    const FString OldClass = CurrentClass(Setting);
    if (OldClass != Expected)
    {
        OutError = {TEXT("stale_precondition"), TEXT("The configured gameplay-framework class changed")};
        OutError.Details->SetStringField(TEXT("expected_class"), Expected);
        OutError.Details->SetStringField(TEXT("current_class"), OldClass);
        return false;
    }
    UClass* Resolved = nullptr;
    if (!ResolveClass(Setting, ClassPath, Resolved, OutError)) return false;
    if (ClassPath == OldClass)
    {
        OutError = {TEXT("invalid_argument"), TEXT("The requested gameplay-framework class is already configured")};
        return false;
    }
    const FString Key = Setting == TEXT("default_game_mode") ? TEXT("GlobalDefaultGameMode") : TEXT("GameInstanceClass");
    if (!Persist(Key, ClassPath, OutError)) return false;
    UGameMapsSettings* Settings = GetMutableDefault<UGameMapsSettings>();
    if (Setting == TEXT("default_game_mode")) UGameMapsSettings::SetGlobalDefaultGameMode(ClassPath);
    else Settings->GameInstanceClass = FSoftClassPath(ClassPath);
    if (CurrentClass(Setting) != ClassPath)
    {
        OutError = {TEXT("internal_error"), TEXT("The persisted gameplay-framework assignment did not update the live settings object")};
        return false;
    }
    OutResult = MakeShared<FJsonObject>();
    OutResult->SetStringField(TEXT("project_hash"), ProjectHash);
    OutResult->SetStringField(TEXT("setting"), Setting);
    OutResult->SetStringField(TEXT("old_class"), OldClass);
    OutResult->SetStringField(TEXT("new_class"), ClassPath);
    OutResult->SetBoolField(TEXT("restart_required"), false);
    OutResult->SetBoolField(TEXT("active_sessions_unaffected"), true);
    OutResult->SetBoolField(TEXT("verified"), true);
    return true;
}
