#pragma once

#include "CoreMinimal.h"

class AIslandGameMode;
class AIslandGameState;
class AIslandProbePeer;
class AVoxelTerrain;
struct FVoxelEdit;

// Drives the normal player tool inputs after a decoy and compares the resulting collision on each peer.
struct FExpeditionDecoyToolProof
{
    bool Tick(AIslandGameMode& Mode, const AIslandGameState& Round);
    bool Passed() const { return bPassed; }
    static void ObserveCommitted(AVoxelTerrain& Terrain, const FVoxelEdit& Edit);

private:
    enum class EStep : uint8 { Place, Settle, CheckReach, WaitImpact, Done };
    EStep Step = EStep::Place;
    int32 Stroke = 0;
    uint32 StartSequence = 0;
    double StepAt = 0.;
    TWeakObjectPtr<AIslandProbePeer> Shooter;
    FTransform OriginalPose;
    FRotator OriginalView;
    FVector Stand = FVector::ZeroVector;
    int32 NormalWidth = 0;
    double NormalThrowTravel = 0.;
    bool bImpactMeasured = false;
    bool bPassed = true;
};
