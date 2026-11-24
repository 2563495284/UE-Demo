#pragma once

#include "Commandlets/Commandlet.h"
#include "ActPartyFeatureCommandlet.generated.h"

UCLASS()
class UActPartyFeatureCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    int32 Main(const FString& Params) override;
};
