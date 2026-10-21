#pragma once
#include "Commandlets/Commandlet.h"
#include "ActKwangCommandlet.generated.h"

/** Inspect the imported pack, or generate ActDemo-owned Kwang assets with -Build. */
UCLASS()
class UActKwangCommandlet : public UCommandlet
{
 GENERATED_BODY()
public:
 UActKwangCommandlet();
 virtual int32 Main(const FString& Params) override;
};
