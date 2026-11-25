#pragma once

#include "PartyVoxelArena.h"
#include "FloodGatesSession.generated.h"

UCLASS()
class AFloodGatesSession final : public APartyVoxelSession
{
    GENERATED_BODY()

public:
    AFloodGatesSession();

private:
    void BuildArena() override;
    void OnArenaStep(float DeltaSeconds) override;
    void OnTerrainEdited(int32 Seat, const FBox& WorldBounds, bool bPlaced) override;
    void CheckChannels();
    bool bCheckWater = true;
    bool bSealed[8][4] = {};
    double DryTime[8] = {};
};
