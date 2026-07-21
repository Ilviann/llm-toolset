#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "UnrealMCPProtocol.h"

class FCompilerResultsLog;
class FUnrealMCPBlueprintInspector;
class UBlueprint;

class FUnrealMCPBlueprintMutator
{
public:
    using FCompile = TFunction<void(UBlueprint*, FCompilerResultsLog&)>;
    using FSave = TFunction<bool(UBlueprint*)>;

    explicit FUnrealMCPBlueprintMutator(
        FUnrealMCPBlueprintInspector& InInspector,
        FCompile InCompile = FCompile(),
        FSave InSave = FSave());

    bool Execute(
        const FString& Command,
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

private:
    bool Create(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);
    bool Compile(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);
    bool Save(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);
    bool ComponentEdit(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);
    bool DefaultEdit(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);
    bool MemberEdit(
        const TSharedPtr<FJsonObject>& Arguments,
        TSharedPtr<FJsonObject>& OutResult,
        FUnrealMCPError& OutError);

    FUnrealMCPBlueprintInspector& Inspector;
    FCompile CompileBlueprint;
    FSave SaveBlueprint;
};
