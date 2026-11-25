#pragma once

#include "PartyVoxelArena.h"
#include "BridgeDashSession.generated.h"

UCLASS()
class ABridgeDashSession final : public APartyVoxelSession
{
    GENERATED_BODY()

public:
    ABridgeDashSession();

private:
    void BuildArena() override;
    void OnArenaStep(float DeltaSeconds) override;
    void OnTerrainEdited(int32 Seat, const FBox& WorldBounds, bool bPlaced) override;
    void FindConnectedDeck(int32 NearBank, int32 FarBank, TSet<FIntVector>& Deck) const;
    void CheckRunner(APartyVoxelPawn& Pawn);
    enum class ECrossing : uint8 { Waiting, NearBank, OnBridge };
    ECrossing Crossing[8] = {};
    TSet<FIntVector> ConnectedDeck[2];
    bool bCheckBridges = true;
};
