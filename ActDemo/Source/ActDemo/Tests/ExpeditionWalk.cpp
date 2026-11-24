#include "Tests/ExpeditionWalk.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/InputSettings.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

AExpeditionWalk::AExpeditionWalk()
{
    PrimaryActorTick.bCanEverTick = true;
    bTreasureRun = FParse::Param(FCommandLine::Get(),TEXT("ExpeditionTreasureWalkQA"));
}

void AExpeditionWalk::ChangeStage(EStage Next)
{
    Stage = Next;
    StageStarted = GetWorld()->GetTimeSeconds();
    LastInput = -5.;
}

void AExpeditionWalk::RecordView(const FString& Name) const
{
    if (!FApp::CanEverRender()) return;
    const FString Output = FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("ExpeditionWalk"));
    IFileManager::Get().MakeDirectory(*Output,true);
    FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::Combine(Output,Name + TEXT(".png"))),true,false);
}

void AExpeditionWalk::StopRun(const FString& Reason)
{
    bPassed = Reason.IsEmpty();
    if (bPassed)
    {
        Frames.Sort();
        const double P95 = Frames.IsEmpty() ? 0. : Frames[FMath::Clamp(FMath::CeilToInt(Frames.Num()*.95)-1,0,Frames.Num()-1)];
        UE_LOG(LogTemp,Display,TEXT("EXPEDITION_WALK_DONE areas=4 meters=%.1f elapsed=%.1fs frameP95=%.2fms"),Meters,GetWorld()->GetTimeSeconds()-WalkStarted,P95);
    }
    else
    {
        UE_LOG(LogTemp,Error,TEXT("EXPEDITION_WALK_FAILED stage=%d waypoint=%d xyz=%s reason=%s"),static_cast<int32>(Stage),Destination,*LastPosition.ToCompactString(),*Reason);
    }
    ChangeStage(EStage::Complete);
}

void AExpeditionWalk::Turn(AIslandPlayerController& Player, FRotator Desired, float DeltaSeconds) const
{
    const FRotator Delta = (Desired - Player.GetControlRotation()).GetNormalized();
    const double MaxChange = DeltaSeconds * 130.;
    double YawScale = 1., PitchScale = 1.;
    if (GetDefault<UInputSettings>()->bEnableLegacyInputScales)
    {
        PRAGMA_DISABLE_DEPRECATION_WARNINGS
        YawScale = Player.GetDeprecatedInputYawScale();
        PitchScale = Player.GetDeprecatedInputPitchScale();
        PRAGMA_ENABLE_DEPRECATION_WARNINGS
    }
    if (!FMath::IsNearlyZero(YawScale)) Player.AddYawInput(FMath::Clamp(Delta.Yaw,-MaxChange,MaxChange)/YawScale);
    if (!FMath::IsNearlyZero(PitchScale)) Player.AddPitchInput(FMath::Clamp(Delta.Pitch,-MaxChange,MaxChange)/PitchScale);
}

bool AExpeditionWalk::WalkTo(AIslandPlayerController& Player, FVector2D Dest, float DeltaSeconds, double Radius) const
{
    const FVector2D Offset = Dest - FVector2D(Player.GetHero()->GetActorLocation());
    if (Offset.Size() < Radius) return true;
    FRotator View = FVector(Offset,0).Rotation();
    View.Pitch = -12.;
    Turn(Player,View,DeltaSeconds);
    // Same analog input handler as WASD. Camera orientation, acceleration, floor and collision are all live.
    if (FMath::Abs(FMath::FindDeltaAngleDegrees(Player.GetControlRotation().Yaw,View.Yaw)) < 22.)
    {
        const double Forward = FMath::Clamp(Offset.Size()/100., .18, 1.);
        FVector2D Move(0.,Forward);
        if (Stage==EStage::Walking || HuntStep==EHuntStep::Approach)
        {
            const FVector From = Player.GetHero()->GetActorLocation() - FVector(0,0,32);
            FHitResult Obstacle;
            const FVector Ahead = FVector(Offset.GetSafeNormal(),0.)*130.;
            const FCollisionQueryParams Query(SCENE_QUERY_STAT(ExpeditionWalkAvoid),false,Player.GetHero());
            if (GetWorld()->LineTraceSingleByChannel(Obstacle,From,From+Ahead,ECC_Visibility,Query)
                && Obstacle.ImpactNormal.Z < .62)
            {
                const FVector Right = FVector::CrossProduct(FVector::UpVector,Ahead).GetSafeNormal();
                const double Side=FVector::DotProduct(Obstacle.ImpactNormal,Right);
                Move = FVector2D(Side<-.06?-1.:1.,.22);
            }
        }
        Player.ApplyMove(Move);
    }
    return false;
}

void AExpeditionWalk::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Now = GetWorld()->GetTimeSeconds();
    if (Stage == EStage::Complete)
    {
        if (Now-StageStarted>1.) FPlatformMisc::RequestExitWithStatus(false,bPassed ? 0 : 1);
        return;
    }
    if (!bTreasureRun && Now-StageStarted>55.)
    {
        StopRun(TEXT("timed out awaiting physical movement or input"));
        return;
    }
    if (bTreasureRun) { TickTreasure(DeltaSeconds); return; }
    auto* Player = Cast<AIslandPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* State = GetWorld()->GetGameState<AIslandGameState>();
    auto* Terrain = AVoxelTerrain::Find(GetWorld());
    if (!Player || !Player->GetHero() || !State || !Terrain || !Terrain->IsReady()) return;
    AVoxelCharacter* Hero = Player->GetHero();
    const FVector Position = Hero->GetActorLocation();
    if (Stage == EStage::Lobby)
    {
        if (!bStarted && State->Phase==EIslandPhase::Waiting && State->CanLaunch())
        {
            Player->ServerStartExpedition();
            bStarted=true;
        }
        if (State->Phase!=EIslandPhase::Hunting || !Player->IsPlaying()) return;
        auto* Dock = AExpeditionDock::FindForTeam(GetWorld(),{Player->GetTeam()},Player->GetTeam());
        if (!Dock) { StopRun(TEXT("landing does not resolve")); return; }
        Route.Add(FVector2D(Dock->GetActorLocation() + Dock->GetActorForwardVector()*720.));
        for (const auto& Circle : Terrain->GetSearchCircles()) Route.Add(FVector2D(Circle.Center));
        WalkStarted=Now;
        LastPosition=Position;
        ChangeStage(EStage::Landing);
        return;
    }
    const double Real=FPlatformTime::Seconds();
    if (LastFrame>0.) Frames.Add((Real-LastFrame)*1000.);
    LastFrame=Real;
    const double Step=FVector::Distance(Position,LastPosition);
    if (Stage!=EStage::Returning && Step>FMath::Max(180.,DeltaSeconds*1500.))
    {
        StopRun(TEXT("unexpected displacement en route"));
        return;
    }
    if (Step<180.) Meters+=Step/100.;
    LastPosition=Position;
    if (Stage==EStage::Landing)
    {
        if (Now-StageStarted<1.) return;
        RecordView(TEXT("00-landing"));
        ChangeStage(EStage::Walking);
        return;
    }
    if (Stage==EStage::Walking)
    {
        if (!WalkTo(*Player,Route[Destination],DeltaSeconds)) return;
        UE_LOG(LogTemp,Display,TEXT("EXPEDITION_WALK_STOP index=%d xyz=%s t=%.2f meters=%.1f"),Destination,*Position.ToCompactString(),Now-WalkStarted,Meters);
        if (Destination==0)
        {
            ++Destination;
            StageStarted=Now;
        }
        else ChangeStage(EStage::Viewing);
        return;
    }
    if (Stage==EStage::Viewing)
    {
        AExpeditionLandmark* Landmark = nullptr;
        for (TActorIterator<AExpeditionLandmark> It(GetWorld()); It; ++It)
            if (It->ClueIndex==Destination-1) { Landmark=*It; break; }
        if (!Landmark) { StopRun(TEXT("chart landmark missing in world")); return; }
        FVector Focus, Extent, Eye;
        FRotator Camera;
        Landmark->GetActorBounds(false,Focus,Extent);
        Player->GetPlayerViewPoint(Eye,Camera);
        const FRotator Desired=(Focus-Eye).Rotation();
        Turn(*Player,Desired,DeltaSeconds);
        const FRotator Delta=(Desired-Camera).GetNormalized();
        if (FMath::Abs(Delta.Yaw)>2. || FMath::Abs(Delta.Pitch)>2.)
        {
            LastInput=-5.;
            return;
        }
        if (LastInput<0.) { LastInput=Now; return; }
        if (Now-LastInput<.35) return;
        RecordView(FString::Printf(TEXT("0%d-zone-%c"),Destination,'A'+Destination-1));
        if (Destination==3)
        {
            ChangeStage(EStage::Digging);
            Pit=Position;
            EscapeDirection=(Route[4]-Route[3]).GetSafeNormal();
            BaselineSequence=Terrain->GetAppliedSequence();
        }
        else if (++Destination>=Route.Num())
        {
            StopRun({});
        }
        else ChangeStage(EStage::Walking);
        return;
    }
    if (Stage==EStage::Digging)
    {
        if (Terrain->GetAppliedSequence()>BaselineSequence && Position.Z<Pit.Z-80. && !Hero->GetCharacterMovement()->IsFalling())
        {
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_WALK_PIT depth=%.1f seq=%u"),Pit.Z-Position.Z,Terrain->GetAppliedSequence());
            RecordView(TEXT("05-far-dig"));
            ChangeStage(EStage::Escaping);
            return;
        }
        if (Terrain->GetAppliedSequence()==BaselineSequence && Now-StageStarted>.6 && Now-LastInput>.8)
        {
            Player->SubmitEdit(EVoxelAction::DigDown);
            LastInput=Now;
        }
        return;
    }
    if (Stage==EStage::Escaping)
    {
        WalkTo(*Player,FVector2D(Pit)+EscapeDirection*650.,DeltaSeconds,10.);
        const double YawError = FMath::Abs(FMath::FindDeltaAngleDegrees(Player->GetControlRotation().Yaw,FVector(EscapeDirection,0.).Rotation().Yaw));
        const double OriginalSurface = VoxelPlayground::BaseHeight(Terrain->Layout,FMath::RoundToInt(Position.X),FMath::RoundToInt(Position.Y));
        if (bClimbing && Position.Z>=OriginalSurface-15.)
        {
            Player->StopClimb();
            bClimbing=false;
        }
        else if (!bClimbing && Position.Z<OriginalSurface-30. && YawError<15.)
        {
            Player->StartClimb();
            bClimbing=true;
        }
        if (Now-LastInput>.90)
        {
            Hero->Jump();
            LastInput=Now;
        }
        else if (Now-LastInput>.12) Hero->StopJumping();
        // Remove the overhanging lip ahead with a regular, range-checked punch, like a player
        // cutting a foothold before climbing. The brush location is resolved by the server.
        if (YawError<15. && Now-StageStarted>2. && Now-LastCarve>1.4 && FVector::Dist2D(Position,Pit)<230. && !Terrain->IsBusy())
        {
            const double TargetZ=Pit.Z-92.-18.;
            const FVector RockTarget(FVector2D(Position)+EscapeDirection*155.,TargetZ);
            Player->SubmitEdit(EVoxelAction::Punch,(RockTarget-Hero->GetAimOrigin()).GetSafeNormal());
            LastCarve=Now;
        }
        // A player can carve a side alcove and return by holding R even when the exit lip is
        // too steep. Identify it separately from reaching the original (uncarved) surface.
        const bool bSurface = FVector::Dist2D(Position,Pit)>435. && FMath::Abs((Position.Z-92.)-OriginalSurface)<36.
            && Hero->GetCharacterMovement()->IsMovingOnGround();
        const bool bSidePassage = Now-StageStarted>9. && FVector::Dist2D(Position,Pit)>185. && Position.Z>Pit.Z-130.;
        if (bSurface || bSidePassage)
        {
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_WALK_EXIT surface=%d sidepassage=%d radius=%.1f"),
                bSurface,bSidePassage,FVector::Dist2D(Position,Pit));
            Player->StopClimb();
            Hero->StopJumping();
            BaselineSequence=Terrain->GetAppliedSequence();
            ChangeStage(EStage::Filling);
        }
        return;
    }
    if (Stage==EStage::Returning)
    {
        if (FVector::Dist2D(Position,Player->StartTransform.GetLocation())<160.)
        {
            Player->StopRecover();
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_WALK_DOCK_RECOVERY seconds=%.2f"),Now-StageStarted);
            ++Destination;
            ChangeStage(EStage::Walking);
        }
        return;
    }
    if (Stage==EStage::Filling)
    {
        if (Terrain->GetAppliedSequence()>BaselineSequence)
        {
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_WALK_RETURNED_FROM_PIT seq=%u xyz=%s"),Terrain->GetAppliedSequence(),*Position.ToCompactString());
            RecordView(TEXT("06-far-fill"));
            ChangeStage(EStage::Returning);
            Player->StartRecover();
            return;
        }
        const FVector2D GroundTarget = FVector2D(Position) - EscapeDirection * 137.;
        const FVector Target(GroundTarget,Terrain->GetDensity()->SurfaceAt(GroundTarget).Get(Position.Z-90.));
        FVector Camera;
        FRotator CameraRotation;
        Player->GetPlayerViewPoint(Camera,CameraRotation);
        Turn(*Player,(Target-Camera).Rotation(),DeltaSeconds);
        if (Now-StageStarted>1. && Now-LastInput>.8)
        {
            Player->SubmitEdit(EVoxelAction::Fill);
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_WALK_FILL_AIM elapsed=%.1f target=%s eye=%s view=%s feedback=%s"),
                Now-StageStarted,*Target.ToCompactString(),*Camera.ToCompactString(),*CameraRotation.ToCompactString(),*Player->GetNotice());
            LastInput=Now;
        }
    }
}
