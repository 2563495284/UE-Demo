#pragma once
#include "Commandlets/Commandlet.h"
#include "ActIslandFrontCommandlet.generated.h"

UCLASS()
class UActIslandFrontCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UActIslandFrontCommandlet();
    int32 Main(const FString& Params) override;
};
