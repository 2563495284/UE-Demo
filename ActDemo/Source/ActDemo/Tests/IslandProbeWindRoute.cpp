#include "Tests/IslandProbe.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandNeutralProp.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelPlayerController.h"

void AIslandProbe::TickWindChain(AIslandNeutralProp& Tower, double Now)
{
    constexpr int32 Seat = 2;
    auto& Hero = *Mode->GetPlayers()[Seat]->GetHero();
    const double HalfHeight = Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Foot = Hero.GetActorLocation() - FVector(0,0,HalfHeight);
    if (Now-StepAt > 12.)
    {
        UE_LOG(LogTemp,Error,TEXT("ROUTE_QA_WIND_STALLED step=%d foot=%s goal=%s peak_cm=%.1f"),
            NeutralStep,*Foot.ToCompactString(),*NeutralWalkTo.ToCompactString(),NeutralPeak);
        Require(false,TEXT("each deliberate wind jump reaches the next mouth or the final bank"));
        return;
    }
    if (NeutralStep==30)
    {
        // Establish only the initial pose. Every subsequent takeoff uses the previous actual landing.
        FVector Floor;
        if (!NeutralFloorPose(Seat,Tower.GetWindJumpStart(0),Floor)) return;
        Place(Seat,Floor+FVector(0,0,HalfHeight+3.));
        SetNeutralStep(31,Now);
        return;
    }
    const int32 Hop = (NeutralStep-31)/2;
    if ((NeutralStep-31)%2==0)
    {
        if (Now-StepAt < .4 || !Hero.GetCharacterMovement()->IsMovingOnGround()) return;
        NeutralWalkFrom = Foot;
        NeutralWalkTo = Tower.GetWindJumpLanding(Hop);
        NeutralPeak = 0.;
        Peers[Seat]->ClientNeutralInput(AIslandProbePeer::NeutralJumpButton,NeutralWalkTo);
        SetNeutralStep(NeutralStep+1,Now);
        return;
    }
    NeutralPeak = FMath::Max(NeutralPeak,Foot.Z-NeutralWalkFrom.Z);
    if (NeutralPeak < 125. || !Hero.GetCharacterMovement()->IsMovingOnGround()) return;
    const double LandingError = FVector::Dist2D(Foot,NeutralWalkTo);
    UE_LOG(LogTemp,Display,TEXT("ROUTE_QA_WIND_HOP hop=%d from=%s landed=%s target=%s horizontal_cm=%.1f peak_cm=%.1f error_cm=%.1f"),
        Hop+1,*NeutralWalkFrom.ToCompactString(),*Foot.ToCompactString(),*NeutralWalkTo.ToCompactString(),
        FVector::Dist2D(NeutralWalkFrom,Foot),NeutralPeak,LandingError);
    if (LandingError>145.)
    {
        Require(false,TEXT("wind carrier lands inside the next mouth without repositioning"));
        return;
    }
    if (Hop==2)
    {
        bWindChainDone = true;
        SetNeutralStep(6,Now);
    }
    else SetNeutralStep(NeutralStep+1,Now);
}
