#pragma once
#include "Commandlets/Commandlet.h"
#include "ActDemoBuildCommandlet.generated.h"

/** Creates the editable demo assets from bundled Epic resources. Existing assets require -Force. */
UCLASS()
class UActDemoBuildCommandlet : public UCommandlet
{
 GENERATED_BODY()
public:
 UActDemoBuildCommandlet();
 virtual int32 Main(const FString& Params) override;
};
