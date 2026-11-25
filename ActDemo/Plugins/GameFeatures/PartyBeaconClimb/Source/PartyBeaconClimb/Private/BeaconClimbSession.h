#pragma once

#include "PartyVoxelArena.h"
#include "BeaconClimbSession.generated.h"

UCLASS()
class ABeaconClimbSession final : public APartyVoxelSession
{
    GENERATED_BODY()

public:
    ABeaconClimbSession();

private:
    void BuildArena() override;
    void OnArenaStep(float DeltaSeconds) override;
};
