#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FUnrealMCPAssetReferenceLiveScanner
{
public:
    void Scan(
        UObject* TargetObject,
        TArray<TSharedPtr<FJsonValue>>& OutRecords,
        TSharedPtr<FJsonObject>& OutStatus,
        int32& OutOpenEditorCount) const;
};
