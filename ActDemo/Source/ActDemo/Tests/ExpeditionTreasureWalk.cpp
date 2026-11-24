#include "Tests/ExpeditionWalk.h"

#include "Island/ExpeditionDock.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Tests/IslandProbe.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "UnrealClient.h"

namespace
{
    void MapKey(AIslandPlayerController& Player, bool bPressed)
    {
        Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::Tab,bPressed?IE_Pressed:IE_Released,bPressed?1.f:0.f));
    }

    // Pick a piece of rock still physically covering the box. Only the development driver
    // uses the server target. GameMode validates the player's reach and resolves the brush itself.
    TOptional<FVector> VisibleRock(AIslandPlayerController& Player, AVoxelTerrain& Terrain, const FVector& Chest)
    {
        const auto& Density = *Terrain.GetDensity();
        const FVector Origin(Density.Grid.Origin);
        const FBox Bounds = FBox::BuildAABB(Chest,IslandRules::ChestExtent);
        constexpr double Spacing = VoxelPlayground::Spacing;
        const FVector Low=(Bounds.Min-Origin)/Spacing, High=(Bounds.Max-Origin)/Spacing;
        const FVector Eye=Player.GetHero()->GetAimOrigin();
        TOptional<FVector> Best;
        double Nearest = MAX_dbl;
        for (int32 Z=FMath::FloorToInt(Low.Z); Z<=FMath::CeilToInt(High.Z); ++Z)
            for (int32 Y=FMath::FloorToInt(Low.Y); Y<=FMath::CeilToInt(High.Y); ++Y)
                for (int32 X=FMath::FloorToInt(Low.X); X<=FMath::CeilToInt(High.X); ++X)
                {
                    const FIntVector Cell(X,Y,Z);
                    if (Density.Sample(Cell)>=0) continue;
                    const FVector Point=Origin+FVector(Cell)*Spacing;
                    const double Distance=FVector::DistSquared(Point,Eye);
                    if (Distance>=Nearest) continue;
                    FHitResult Hit;
                    if (!Terrain.TraceRock(Eye,Point+(Point-Eye).GetSafeNormal()*22.,Player.GetHero(),Hit)
                        || FVector::DistSquared(Hit.ImpactPoint,Eye)>FMath::Square(AVoxelGameMode::AimReach-1.)) continue;
                    Nearest=Distance;
                    Best=Hit.ImpactPoint;
                }
        return Best;
    }
}

void AExpeditionWalk::SetHuntStep(EHuntStep Next)
{
    HuntStep=Next;
    StageStarted=GetWorld()->GetTimeSeconds();
    LastInput=-5.;
}

void AExpeditionWalk::TickTreasure(float DeltaSeconds)
{
#if !UE_BUILD_SHIPPING
    const double Now=GetWorld()->GetTimeSeconds();
    auto* Player=Cast<AIslandPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* State=GetWorld()->GetGameState<AIslandGameState>();
    auto* Mode=GetWorld()->GetAuthGameMode<AIslandGameMode>();
    if (!Player || !Mode || !State || !Player->IsLocalController() || !Player->GetHero()) return;
    if (HuntStep==EHuntStep::Done) return;
    if (State->Phase==EIslandPhase::Finished && TreasureIndex==IslandRules::PrizeIndex
        && State->Winner==Player->GetTeam() && Player->GetLocalClues().Complete())
    {
        RecordView(TEXT("10-treasure-result"));
        UE_LOG(LogTemp,Display,TEXT("EXPEDITION_TREASURE_WALK_DONE clues=4 opened=5 elapsed=%.2fs meters=%.1f"),Now-WalkStarted,Meters);
        SetHuntStep(EHuntStep::Done);
        return; // The Slate walkthrough owns the result button and return to the original room.
    }
    if (State->Phase!=EIslandPhase::Hunting || !Mode->IsPlaying() || !Mode->GetTerrain() || !Mode->GetTerrain()->IsReady()) return;
    AVoxelCharacter& Hero=*Player->GetHero();
    AVoxelTerrain& Terrain=*Mode->GetTerrain();
    const FVector Position=Hero.GetActorLocation();
    if (HuntStep==EHuntStep::AwaitPlay)
    {
        if (Player->GetViewTarget()!=&Hero || !Player->IsVoxelInputEnabled()) return;
        WalkStarted=Now;
        LastPosition=Position;
        SetHuntStep(EHuntStep::ReadChart);
        return;
    }
    const double StepDistance=FVector::Distance(Position,LastPosition);
    if (HuntStep!=EHuntStep::Recall && StepDistance>FMath::Max(200.,DeltaSeconds*1800.))
    {
        StopRun(TEXT("unexpected displacement during expedition walkthrough"));
        return;
    }
    if (StepDistance<200.) Meters+=StepDistance/100.;
    LastPosition=Position;
    if (Now-StageStarted>90.)
    {
        StopRun(FString::Printf(TEXT("hunt stage %d treasure %d timed out: %s"),static_cast<int32>(HuntStep),TreasureIndex,*Player->GetNotice()));
        return;
    }

    bool bExposed=false;
    if (!AIslandProbe::TryGetWalkTarget(Mode,TreasureIndex,ChestTarget,bExposed)) return;
    if (HuntStep==EHuntStep::ReadChart)
    {
        if (Now-StageStarted<.65) return;
        if (!bChartHeld) { MapKey(*Player,true); bChartHeld=true; return; }
        if (!Player->IsMapOpen()) return;
        if (Now-StageStarted<2.3) return;
        MapKey(*Player,false);
        bChartHeld=false;
        UE_LOG(LogTemp,Display,TEXT("EXPEDITION_TREASURE_CHART clues=%d crossing=%d"),Player->GetLocalClues().Count(),Player->GetLocalClues().Complete());
        Route.Reset();
        const auto* Dock=AExpeditionDock::FindForTeam(GetWorld(),{Player->GetTeam()},Player->GetTeam());
        if (!Dock) { StopRun(TEXT("no departure dock")); return; }
        Route.Add(FVector2D(Dock->GetActorLocation()+Dock->GetActorForwardVector()*730.));
        if (TreasureIndex>0) Route.Add(FVector2D(Dock->GetActorLocation()).GetSafeNormal()*1250.);
        Route.Add(FVector2D(ChestTarget));
        Destination=0;
        SetHuntStep(EHuntStep::Approach);
        return;
    }
    if (HuntStep==EHuntStep::Approach)
    {
        const bool bFinal=Destination==Route.Num()-1;
        if (!WalkTo(*Player,Route[Destination],DeltaSeconds,bFinal?19.:80.)) return;
        if (++Destination<Route.Num()) { StageStarted=Now; return; }
        DigAttempts=0;
        UE_LOG(LogTemp,Display,TEXT("EXPEDITION_TREASURE_SITE box=%d traveled=%.1fm"),TreasureIndex,Meters);
        SetHuntStep(EHuntStep::Excavate);
        return;
    }
    if (HuntStep==EHuntStep::Excavate)
    {
        if (bExposed) { SetHuntStep(EHuntStep::Open); return; }
        if (Now-StageStarted<.4 || Terrain.IsBusy()) return;
        const FVector2D Offset=FVector2D(Position)-FVector2D(ChestTarget);
        if (Offset.Size()>85. && Hero.GetCharacterMovement()->IsMovingOnGround())
        {
            WalkTo(*Player,FVector2D(ChestTarget),DeltaSeconds,32.);
            return;
        }
        if (!Hero.GetCharacterMovement()->IsMovingOnGround()) return;
        const double FootZ=Position.Z-Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        TOptional<FVector> Rock;
        if (FootZ<=ChestTarget.Z+100.) Rock=VisibleRock(*Player,Terrain,ChestTarget);
        if (!Rock) Rock=HighestSupportingRock(*Player,Terrain);
        if (!Rock) Rock=VisibleRock(*Player,Terrain,ChestTarget);
        if (!Rock) return;
        const FVector Aim=(*Rock-Hero.GetAimOrigin()).GetSafeNormal();
        Turn(*Player,Aim.Rotation(),DeltaSeconds);
        if (Now-LastInput<.61) return;
        // This is the same GAS Dig event as a single left press, with a fresh physical ray each time.
        // It cannot choose a brush centre or edit the density directly.
        Player->SubmitEdit(EVoxelAction::Punch,Aim);
        if ((DigAttempts%12)==0)
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_TREASURE_SHOVEL box=%d attempt=%d foot=%.1f targetZ=%.1f seq=%u"),
                TreasureIndex,DigAttempts,FootZ,ChestTarget.Z,Terrain.GetAppliedSequence());
        LastInput=Now;
        ++DigAttempts;
        return;
    }
    if (HuntStep==EHuntStep::Open)
    {
        const bool bCollected=TreasureIndex<IslandRules::ClueCount && (Player->GetLocalClues().Mask&(1<<TreasureIndex))!=0;
        if (bCollected)
        {
            if (Now-LastInput<.65) return;
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_TREASURE_OPEN box=%d attempts=%d clues=%d"),TreasureIndex,DigAttempts,Player->GetLocalClues().Count());
            ++TreasureIndex;
            SetHuntStep(EHuntStep::Recall);
            return;
        }
        Turn(*Player,(ChestTarget-Hero.GetAimOrigin()).Rotation(),DeltaSeconds);
        if (Now-LastInput>.65)
        {
            // Same input event bound to E in the shipping player controller.
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E,IE_Pressed,1.f));
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E,IE_Released,0.f));
            LastInput=Now;
        }
        return;
    }
    if (HuntStep==EHuntStep::Recall)
    {
        if (!bRecallHeld) { Player->StartRecover(); bRecallHeld=true; return; }
        if (FVector::Dist2D(Position,Player->StartTransform.GetLocation())>160.) return;
        Player->StopRecover();
        bRecallHeld=false;
        UE_LOG(LogTemp,Display,TEXT("EXPEDITION_TREASURE_RECALL clues=%d elapsed=%.2fs"),Player->GetLocalClues().Count(),Now-StageStarted);
        SetHuntStep(EHuntStep::ReadChart);
    }
#endif
}
