#pragma once

#include "Commandlets/Commandlet.h"
#include "ActVoxelMapCommandlet.generated.h"

UCLASS()
class UActVoxelMapCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UActVoxelMapCommandlet();
    int32 Main(const FString& Params) override;
};
