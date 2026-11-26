#pragma once

#include "PartyVoxelArena.h"
#include "CrystalHuntSession.generated.h"

UCLASS()
class ACrystalHuntSession final : public APartyVoxelSession
{
    GENERATED_BODY()

public:
    ACrystalHuntSession();

private:
    void BuildArena() override;
    void OnTerrainEdited(int32 Seat, const FBox& WorldBounds, bool bPlaced) override;
    TSet<FIntVector> Pearls;
};
