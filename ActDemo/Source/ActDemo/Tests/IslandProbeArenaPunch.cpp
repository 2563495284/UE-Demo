#include "Tests/IslandProbe.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandPlayerController.h"
#include "PartyVoxelArena.h"
#include "TimerManager.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    enum ETrial { Miss, Cancel, Tap, Half, Full, Over, Count };
    constexpr float Hold[] = {.25f,.55f,.035f,.60f,1.48f,2.10f};

    bool FlatFloor(const APartyVoxelSession& Arena,FVector P,FHitResult& Hit)
    {
        return Arena.GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(0,0,160),P-FVector(0,0,160),ECC_Visibility)
            && Hit.GetActor()==&Arena && Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(TEXT("PartyDensitySurface"))
            && Hit.ImpactNormal.Z>.995;
    }
}

void AIslandProbePeer::ClientTryArenaPunch_Implementation(int32 Round,int32 Trial,FVector Target)
{
    ArenaPunchRound=Round;
    ArenaPunchTrial=Trial;
    ArenaPunchTarget=Target;
    ArenaPunchPressedAt=-1.;
    bPunchViewWasFirst=GetOwner<AIslandPlayerController>()->WantsFirstPersonView();
}

void AIslandProbePeer::ServerTriedArenaPunch_Implementation(int32 Round,int32 Trial)
{
    TriedPunchRound=Round;
    TriedPunchTrial=Trial;
}

void AIslandProbePeer::TickArenaPunchInput(AIslandPlayerController& Player)
{
    auto* Pawn=Cast<APartyVoxelPawn>(Player.GetPawn());
    auto* Hero=Player.GetHero();
    if ((!Hero && (!Pawn || !Pawn->GetSession())) || Player.GetViewTarget()!=Player.GetPawn()) return;
    if (!Player.WantsFirstPersonView())
    {
        Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V,IE_Pressed,1.f));
        Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V,IE_Released,0.f));
        return;
    }
    if (Pawn && !Pawn->IsInFirstPersonView()) return;
    const int32 Trial=ArenaPunchTrial;
    const double Now=FPlatformTime::Seconds();
    const double Held=ArenaPunchPressedAt<0. ? 0. : Now-ArenaPunchPressedAt;
    // The full punch starts with the reticle pointed into empty sky, then turns to the floor.
    const bool bSky=Trial==Miss || (Trial==Full && Held<.34);
    const FVector Eye=Hero ? Hero->GetAimOrigin() : Pawn->GetPawnViewLocation();
    const FRotator Desired=bSky ? FRotator(66,0,0) : (ArenaPunchTarget-Eye).Rotation();
    Player.SetControlRotation(Desired);
    FHitResult Hit;
    const bool bMaze=Pawn && Pawn->GetSession()->IsChaseGame();
    bool bReached=false;
    if (Hero)
    {
        FVector Camera;
        FRotator View;
        Player.GetPlayerViewPoint(Camera,View);
        if (!View.Equals(Desired,1.)) return;
        bReached=GetWorld()->LineTraceSingleByChannel(Hit,Camera,Camera+View.Vector()*235.,ECC_Visibility,
            FCollisionQueryParams(SCENE_QUERY_STAT(FieldPunchReticle),false,Hero));
    }
    else if (bMaze)
        bReached=Pawn->GetSession()->GetChaseAim(*Pawn,Eye,Pawn->GetToolAimDirection())!=EPartyChaseAim::None;
    else bReached=Pawn->AimTerrain(Hit) && Hit.Distance<=235.;
    if (bSky ? bReached : !bReached || (!bMaze && !Hit.ImpactPoint.Equals(ArenaPunchTarget,3.))) return;
    if (ArenaPunchPressedAt<0.)
    {
        Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Pressed,1.f));
        ArenaPunchPressedAt=Now;
        return;
    }
    const float Alpha=Hero ? Player.GetPunchChargeAlpha() : Pawn->GetPunchChargeAlpha();
    if (Held<Hold[Trial] || Alpha<=0.f) return;
    if (Trial==Cancel) static_cast<APlayerController&>(Player).FlushPressedKeys();
    Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Released,0.f));
    UE_LOG(LogTemp,Display,TEXT("ARENA_PUNCH_INPUT round=%d trial=%d held=%.3f alpha=%.3f sky=%d retarget=%d flush=%d"),
        ArenaPunchRound,Trial,Held,Alpha,bSky,Trial==Full,Trial==Cancel);
    // Restore the camera only after the polling pawn has processed the release.
    if (!bPunchViewWasFirst)
    {
        TWeakObjectPtr<AIslandPlayerController> Observer(&Player);
        FTimerHandle Timer;
        GetWorld()->GetTimerManager().SetTimer(Timer,[Observer]
        {
            if (!Observer.IsValid()) return;
            Observer->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V,IE_Pressed,1.f));
            Observer->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V,IE_Released,0.f));
        },.35f,false);
    }
    ServerTriedArenaPunch(ArenaPunchRound,Trial);
    ArenaPunchTrial=INDEX_NONE;
}

bool AIslandProbe::FindArenaPunchSite(APartyVoxelSession& Arena,APartyVoxelPawn& Pawn,FVector& Point)
{
    // All trial sites use the same sub-cell alignment. Survey each strip and its approach before teleporting.
    const FVector Original=PunchOriginalPose.GetLocation();
    const FIntVector Origin=Arena.CellAt(Original);
    // Keep the reef's checkpoint and the bridge's bank and submerged footings pristine for the scoring run.
    const int32 MaxX=Mode->CurrentPartyFeature==TEXT("PartyReefRun") ? 3 :
        Mode->CurrentPartyFeature==TEXT("PartyBridgeDash") ? 1 : 12;
    for (bool RequireThick : {true,false})
      for (int32 Y=-4; Y<=13; ++Y)
        for (int32 X=-6; X<=MaxX; ++X)
        {
            const FVector Near=Arena.CellCenter(Origin+FIntVector(X,Y,0));
            bool bSeparate=true;
            for (const FVector& Previous : PunchSites)
                if (FVector::DistSquared2D(Near,Previous)<FMath::Square(295.)) bSeparate=false;
            if (!bSeparate) continue;
            FHitResult Center;
            if (!FlatFloor(Arena,Near,Center) || FMath::Abs(Center.ImpactPoint.Z-Original.Z+Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight())>12.) continue;
            // Prefer a floor with solid samples below the deepest possible cut.
            if (RequireThick && Arena.TerrainDensityAt(Center.ImpactPoint-FVector(0,0,125.))>=0.f) continue;
            bool bFlat=true;
            for (int32 R=0; R<=160 && bFlat; R+=10)
            {
                FHitResult Edge;
                bFlat=FlatFloor(Arena,Center.ImpactPoint+FVector(0,R,0),Edge) && FMath::Abs(Edge.ImpactPoint.Z-Center.ImpactPoint.Z)<.5;
            }
            if (!bFlat) continue;
            FHitResult Approach;
            if (!FlatFloor(Arena,Center.ImpactPoint-FVector(112,0,0),Approach) || FMath::Abs(Approach.ImpactPoint.Z-Center.ImpactPoint.Z)>.5) continue;
            const FVector Stand=Approach.ImpactPoint+FVector(0,0,Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.);
            const FCollisionShape Shape=FCollisionShape::MakeCapsule(Pawn.GetCapsuleComponent()->GetScaledCapsuleRadius(),Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            if (GetWorld()->OverlapBlockingTestByChannel(Stand,FQuat::Identity,ECC_Pawn,Shape,FCollisionQueryParams(SCENE_QUERY_STAT(PunchTrialStand),false,&Pawn))) continue;
            FHitResult Aim;
            if (!GetWorld()->LineTraceSingleByChannel(Aim,Stand+FVector(0,0,Pawn.BaseEyeHeight),Center.ImpactPoint-FVector(0,0,1),ECC_Visibility,
                    FCollisionQueryParams(SCENE_QUERY_STAT(PunchTrialSight),false,&Pawn)) || !Aim.ImpactPoint.Equals(Center.ImpactPoint,3.)) continue;
            if (!Pawn.TeleportTo(Stand,Pawn.GetActorRotation(),false,true)) continue;
            Pawn.GetCharacterMovement()->StopMovementImmediately();
            Pawn.ForceNetUpdate();
            Point=Center.ImpactPoint;
            return true;
        }
    return false;
}

bool AIslandProbe::TickArenaPunchTrials(APartyVoxelSession& Arena)
{
    const int32 Trial=TerrainStage-4, Round=GameCount+1;
    if (Trial>=Count) return true;
    // The client owns physical inputs. A second and third process independently resample committed collision.
    const int32 Shooter=Peers.Num()>1 ? 1 : 0;
    auto* Pawn=CastChecked<APartyVoxelPawn>(Mode->GetPlayers()[Shooter]->GetPawn());
    if (!bPunchTrialSent)
    {
        if (!Trial)
        {
            PunchSites.Add(TerrainSamplePoint);
            PunchOriginalPose=Pawn->GetActorTransform();
        }
        if (Trial==Miss || Trial>=Tap)
        {
            if (!FindArenaPunchSite(Arena,*Pawn,TerrainSamplePoint))
            {
                Require(false,TEXT("fresh flat punch trial strip and reachable approach"));
                return false;
            }
            PunchSites.Add(TerrainSamplePoint);
        }
        TerrainInputSequence=Arena.GetAppliedEditCount();
        Peers[Shooter]->ClientTryArenaPunch(Round,Trial,TerrainSamplePoint);
        bPunchTrialSent=true;
        return false;
    }
    if (Peers[Shooter]->TriedPunchRound!=Round || Peers[Shooter]->TriedPunchTrial!=Trial) return false;
    const int32 Sequence=TerrainInputSequence+int32(Trial>=Tap);
    if (Arena.GetAppliedEditCount()<Sequence) return false;
    Require(Arena.GetAppliedEditCount()==Sequence,TEXT("each release commits once and empty or cancelled punches commit nothing"));
    if (!bTerrainSampleRequested)
    {
        for (auto& Peer : Peers)
            Peer->ClientSampleArenaTerrain(Round,Mode->CurrentPartyFeature,TerrainStage,TerrainSamplePoint,Sequence);
        bTerrainSampleRequested=true;
        return false;
    }
    for (auto& Peer : Peers)
        if (Peer->SampledTerrainRound!=Round || Peer->SampledTerrainStage!=TerrainStage) return false;
    bTerrainSampleRequested=false;
    bPunchTrialSent=false;
    ++TerrainStage;
    if (Trial==Over)
    {
        Pawn->TeleportTo(PunchOriginalPose.GetLocation(),PunchOriginalPose.Rotator(),false,true);
        Pawn->ForceNetUpdate();
    }
    return Trial==Over;
}
