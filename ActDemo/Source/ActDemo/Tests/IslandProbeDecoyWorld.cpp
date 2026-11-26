#include "Tests/IslandProbe.h"

#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
TArray<FIntVector> ReadSails(UWorld* World, bool bTestSight)
{
    TArray<FIntVector> Sails;
    for (TActorIterator<AIslandNeutralProp> It(World); It; ++It)
    {
        if (!It->CanToggleSail()) continue;
        const FTransform Pose = It->GetActorTransform();
        int32 Phase = It->IsSailLowered() ? 1 : 0;
        if (bTestSight)
        {
            const bool bObscures = AIslandNeutralProp::AnySailObscures(*World,
                Pose.TransformPosition({-100.,0.,145.}), Pose.TransformPosition({100.,0.,145.}));
            if (bObscures != (Phase == 1)) Phase = INDEX_NONE;
        }
        Sails.Emplace(FMath::RoundToInt(Pose.GetLocation().X), FMath::RoundToInt(Pose.GetLocation().Y), Phase);
    }
    Sails.Sort([](const FIntVector& A, const FIntVector& B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
    return Sails;
}

bool FindDecoyFloor(const AVoxelTerrain& Terrain, FVector Where, FHitResult& Floor)
{
    return Terrain.TraceRock({Where.X, Where.Y, 4500.}, {Where.X, Where.Y, -1500.}, nullptr, Floor);
}
}

void AIslandProbe::SnapshotDecoyWorld()
{
    const auto Plan = Mode->PlanDecoyWorldEvents();
    DecoyWorldMask = Plan.Mask();
    DecoyWorldStage = 0;
    DecoyRampPeak.Reset();
    DecoyRampFloors.Reset();
    DecoyWorldInputAt = 0.;
    DecoySailsBefore = ReadSails(GetWorld(), false);
    auto& Terrain = *Mode->GetTerrain();
    DecoyTerrainSequence = Terrain.GetAppliedSequence();
    for (FVector P : Plan.SandCenters)
    {
        FHitResult Before;
        if (FindDecoyFloor(Terrain, P, Before)) DecoyRampFloors.Add(Before.ImpactPoint);
    }

}

void AIslandProbePeer::ClientObserveDecoyWorld_Implementation(int32 Round, int32 Stage, EIslandDecoyEvent Event,
    FVector Target, FVector2D SpentChest, uint32 Sequence, uint32 Checksum, const TArray<FIntVector>& Sails)
{
    auto* Hero = CastChecked<AIslandPlayerController>(GetOwner())->GetHero();
    auto* Terrain = AVoxelTerrain::Find(GetWorld());
    if (!Hero || !Terrain || !Terrain->IsReady() || Terrain->IsBusy()
        || Terrain->GetAppliedSequence() != Sequence || Terrain->GetChecksum() != Checksum) return;
    const auto* State = GetWorld()->GetGameState<AIslandGameState>();
    if (!State || State->PartyOutcome != EIslandChestOutcome::Decoy || State->PartyDecoyEvent != Event
        || State->SpentFalsePoints.Num() != Round
        || !State->SpentFalsePoints.ContainsByPredicate([&](const FVector2D& P) { return P.Equals(SpentChest, .02); })) return;
    if (ReadSails(GetWorld(), Event == EIslandDecoyEvent::SailGust) != Sails) return;

    double Observed = 0.;
    if (Event == EIslandDecoyEvent::CrewRally)
    {
        Observed = FVector::Dist2D(Target, Hero->GetActorLocation());
        if (Observed > 65.) return;
    }
    else if (Event == EIslandDecoyEvent::SandRamp)
    {
        FHitResult Floor;
        if (!FindDecoyFloor(*Terrain, Target, Floor) || FMath::Abs(Floor.ImpactPoint.Z - Target.Z) > 2.) return;
        if (Stage == 0 && !Hero->GetCharacterMovement()->IsWalkable(Floor)) return;
        Observed = Floor.ImpactPoint.Z;
    }
    UE_LOG(LogTemp, Display, TEXT("DECOY_QA_WORLD round=%d stage=%d event=%d seq=%u crc=%08x sails=%d measured_cm=%.1f"),
        Round, Stage, int32(Event), Sequence, Checksum, Sails.Num(), Observed);
    ServerObservedDecoyWorld(Round, Stage);
}

void AIslandProbePeer::ServerObservedDecoyWorld_Implementation(int32 Round, int32 Stage)
{
    if (Round > DecoyObservedRound || (Round == DecoyObservedRound && Stage > DecoyObservedStage))
    {
        DecoyObservedRound = Round;
        DecoyObservedStage = Stage;
    }
}

bool AIslandProbe::TickDecoyWorld(EIslandDecoyEvent Event, double Now)
{
    auto& Terrain = *Mode->GetTerrain();
    const bool bRally = Event == EIslandDecoyEvent::CrewRally;
    const bool bSand = Event == EIslandDecoyEvent::SandRamp;
    const bool bSail = Event == EIslandDecoyEvent::SailGust;
    if (!bRally && !bSand && !bSail) return true;
    if (Terrain.IsBusy()) return false;

    auto Sails = DecoySailsBefore;
    if (bSail)
    {
        if (Sails.IsEmpty()) { Require(false, TEXT("a gust needs a surviving sail")); return false; }
        for (FIntVector& Sail : Sails) Sail.Z = 1 - Sail.Z;

    }

    // Let the player shovel action finish before taking the second sample.
    if (DecoyWorldStage == 1 && bNeutralWaitingInput)
    {
        if (Now >= NeutralSendAt)
        {
            Peers[NeutralPendingSeat]->ClientNeutralInput(NeutralPendingAction, NeutralAimPoint);
            bNeutralWaitingInput = false;
            DecoyWorldInputAt = Now;
        }
        return false;
    }
    const uint32 Sequence = DecoyTerrainSequence + (bSand ? 1 + DecoyWorldStage : 0);
    const bool bMatches = Terrain.GetAppliedSequence() == Sequence && ReadSails(GetWorld(), bSail) == Sails;
    if (!bMatches)
    {
        if (DecoyWorldStage == 1 && Now - DecoyWorldInputAt > 6.)
            Require(false, TEXT("manual action must edit the sand exactly once"));
        return false;
    }

    FVector Target = FVector::ZeroVector;
    if (bSand)
    {
        if (!DecoyRampPeak.IsSet())
        {
            double HighestRise = 0.;
            for (const FVector Before : DecoyRampFloors)
            {
                FHitResult Floor;
                if (FindDecoyFloor(Terrain, Before, Floor) && Floor.ImpactPoint.Z - Before.Z > HighestRise
                    && Mode->GetPlayers()[0]->GetHero()->GetCharacterMovement()->IsWalkable(Floor))
                {
                    HighestRise = Floor.ImpactPoint.Z - Before.Z;
                    DecoyRampPeak = Floor.ImpactPoint;
                }
            }
            if (HighestRise < 35.) { Require(false, TEXT("sand ramp raised a walkable surface above the previous ground")); return false; }
            const double FromChest = FVector::Dist2D(*DecoyRampPeak, Mode->HiddenLayout.GetCenter(NextDig));
            UE_LOG(LogTemp, Display, TEXT("DECOY_QA_RAMP round=%d rise_cm=%.1f from_chest_cm=%.1f"), GameCount+1, HighestRise, FromChest);
        }
        FHitResult Floor;
        if (!FindDecoyFloor(Terrain, *DecoyRampPeak, Floor)) return false;
        Target = Floor.ImpactPoint;
        if (DecoyWorldStage == 1)
            Require(DecoyRampPeak->Z - Target.Z > 8., TEXT("a player lowered the event's walkable ramp using the normal shovel"));
    }

    bool bConfirmed = true;
    for (int32 Seat = 0; Seat < Peers.Num(); ++Seat)
    {
        if (Peers[Seat]->DecoyObservedRound == GameCount+1 && Peers[Seat]->DecoyObservedStage == DecoyWorldStage) continue;
        bConfirmed = false;
        if (bRally) Target = Mode->GetPlayers()[Seat]->GetHero()->GetActorLocation();
        Peers[Seat]->ClientObserveDecoyWorld(GameCount+1, DecoyWorldStage, Event, Target, FVector2D(Mode->HiddenLayout.GetCenter(NextDig)),
            Sequence, Terrain.GetChecksum(), Sails);
    }
    if (!bConfirmed) return false;
    if (bRally || bSail || DecoyWorldStage == 1) return true;

    NeutralDigLane(0, *DecoyRampPeak);
    if (!bNeutralWaitingInput) { Require(false, TEXT("the player can reach the event sand")); return false; }
    DecoyWorldStage = 1;
    return false;
}
