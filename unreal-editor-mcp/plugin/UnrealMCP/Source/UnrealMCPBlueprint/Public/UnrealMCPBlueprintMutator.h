#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class FCompilerResultsLog;
class FUnrealMCPBlueprintInspector;
class UBlueprint;

class UNREALMCPBLUEPRINT_API FUnrealMCPBlueprintMutator
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
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);

private:
    bool Create(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool Compile(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool Save(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool ComponentEdit(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool DefaultEdit(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool MemberEdit(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool FunctionEdit(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool LocalVariableEdit(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool MacroEdit(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);
    bool CustomEventEdit(
        const TSharedPtr<FUnrealMCPRecord>& Arguments,
        TSharedPtr<FUnrealMCPRecord>& OutResult,
        FUnrealMCPError& OutError);

    FUnrealMCPBlueprintInspector& Inspector;
    FCompile CompileBlueprint;
    FSave SaveBlueprint;
};
