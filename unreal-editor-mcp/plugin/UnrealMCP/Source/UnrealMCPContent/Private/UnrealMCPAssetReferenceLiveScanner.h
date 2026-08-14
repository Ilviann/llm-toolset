#pragma once

#include "CoreMinimal.h"
#include "UnrealMCPWireTypes.h"

class FUnrealMCPAssetReferenceLiveScanner
{
public:
    void Scan(
        UObject* TargetObject,
        TArray<TSharedPtr<FUnrealMCPValue>>& OutRecords,
        TSharedPtr<FUnrealMCPRecord>& OutStatus,
        int32& OutOpenEditorCount) const;
};
