#pragma once

#include "Commandlets/Commandlet.h"
#include "ActIslandVoxelSourcesCommandlet.generated.h"

UCLASS()
class UActIslandVoxelSourcesCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UActIslandVoxelSourcesCommandlet();
    int32 Main(const FString& Params) override;

    UFUNCTION(BlueprintCallable, Category="Expedition|Art")
    static bool RebuildSourceArchive();
};
