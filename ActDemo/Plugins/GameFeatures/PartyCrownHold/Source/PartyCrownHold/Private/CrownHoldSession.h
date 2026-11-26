#pragma once

#include "PartyVoxelArena.h"
#include "CrownHoldSession.generated.h"

UCLASS()
class ACrownHoldSession final : public APartyVoxelSession
{
    GENERATED_BODY()

public:
    ACrownHoldSession();

private:
    void BuildArena() override;
    void OnArenaStep(float DeltaSeconds) override;
    int32 LitCrown = INDEX_NONE;
    double HoldTime[8] = {};
};
