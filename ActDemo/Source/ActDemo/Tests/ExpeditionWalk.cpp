#include "Tests/ExpeditionWalk.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandSceneryVoxels.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"
#include "Voxel/VoxelGameMode.h"
#include "Island/ExpeditionMudBall.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/InputSettings.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
    void VerifyWreckPreviewInput(AIslandPlayerController& Player, AIslandNeutralProp& Ship)
    {
        auto* Hero = Player.GetHero();
        FHitResult Hit;
        const FVector Eye = Hero->GetAimOrigin();
        const FVector Aim = Player.GetExcavationAim(*Hero);
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(WreckPreviewFirstDig),false,Hero);
        if (!AIslandNeutralProp::TraceShovel(*Player.GetWorld(),Hit,Eye,Eye+Aim*AVoxelGameMode::AimReach,Query)
            || Hit.GetActor() != &Ship || !Hit.GetComponent()->IsA<UIslandMeshVoxels>())
        {
            UE_LOG(LogTemp,Error,TEXT("WRECK_FIRST_DIG_FAILED crosshair=%s standing=%s aim=%s"),
                *GetNameSafe(Hit.GetActor()),*Hero->GetActorLocation().ToCompactString(),*Aim.ToCompactString());
            FPlatformMisc::RequestExitWithStatus(false,1);
            return;
        }
        auto* Field = CastChecked<UIslandMeshVoxels>(Hit.GetComponent());
        const int32 Before = Field->RemainingCount();
        Player.SetExpeditionDigHeld(true);
        Player.SetExpeditionDigHeld(false);
        FTimerHandle Timer;
        Player.GetWorldTimerManager().SetTimer(Timer,FTimerDelegate::CreateWeakLambda(Field,[Field,Before]()
        {
            const int32 After = Field->RemainingCount();
            UE_LOG(LogTemp,Display,TEXT("WRECK_FIRST_DIG_RESULT part=%s before=%d after=%d"),*Field->SourcePart()->GetName(),Before,After);
            FPlatformMisc::RequestExitWithStatus(false,After < Before ? 0 : 1);
        }),.4f,false);
    }
}

AExpeditionWalk::AExpeditionWalk()
{
    PrimaryActorTick.bCanEverTick = true;
    bCrewFilm=FParse::Param(FCommandLine::Get(),TEXT("CrewPunchFilm")) || FParse::Param(FCommandLine::Get(),TEXT("CrewPunchFilmPair")) || FParse::Param(FCommandLine::Get(),TEXT("CrewShapeFilm"));
    if (bCrewFilm) { bReplicates=true; bAlwaysRelevant=true; }
    bSlopePitRun = FParse::Param(FCommandLine::Get(),TEXT("ExpeditionSlopePitQA"));
    bTreasureRun = FParse::Param(FCommandLine::Get(),TEXT("ExpeditionTreasureWalkQA"));
    bArtTour = FParse::Param(FCommandLine::Get(),TEXT("ExpeditionArtTour"));
    bWreckPreview = FParse::Param(FCommandLine::Get(),TEXT("WreckPreview"));
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
        if (bSlopePitRun)
        {
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_SLOPE_PIT_DONE meters=%.1f elapsed=%.1fs"),Meters,GetWorld()->GetTimeSeconds()-WalkStarted);
        }
        else UE_LOG(LogTemp,Display,TEXT("EXPEDITION_WALK_DONE areas=4 meters=%.1f elapsed=%.1fs frameP95=%.2fms"),Meters,GetWorld()->GetTimeSeconds()-WalkStarted,P95);
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

TOptional<FVector> AExpeditionWalk::HighestSupportingRock(AIslandPlayerController& Player, AVoxelTerrain& Terrain) const
{
    AVoxelCharacter& Hero = *Player.GetHero();
    const FVector Eye = Hero.GetAimOrigin();
    const FVector Feet = Hero.GetActorLocation() - FVector::UpVector * Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    TOptional<FVector> Best;
    // Sample the player's actual standing footprint. A capsule can bridge a thin lip even when
    // the ray through its exact centre already reaches the bottom.
    const FVector2D Offsets[] = {{0,0}, {35,0}, {-35,0}, {0,35}, {0,-35}, {25,25}, {-25,25}, {25,-25}, {-25,-25}};
    for (const FVector2D& Offset : Offsets)
    {
        const FVector End = Feet + FVector(Offset, -82.);
        FHitResult Hit;
        if (!Terrain.TraceRock(Eye, End, &Hero, Hit)) continue;
        if (!Best || Hit.ImpactPoint.Z > Best->Z + 2.) Best = Hit.ImpactPoint;
    }
    return Best;
}

void AExpeditionWalk::TickWreckPreview()
{
    auto& World = *GetWorld();
    auto* Player = Cast<AIslandPlayerController>(World.GetFirstPlayerController());
    auto* State = World.GetGameState<AIslandGameState>();
    auto* Mode = World.GetAuthGameMode<AIslandGameMode>();
    auto* Terrain = AVoxelTerrain::Find(&World);
    if (!Player || !Player->GetHero() || !State || !Mode || !Terrain || !Terrain->IsReady()) return;
    if (!bStarted && State->Phase == EIslandPhase::Waiting && State->CanLaunch())
    {
        if (FParse::Param(FCommandLine::Get(),TEXT("PigPunchPreview"))) Player->ServerChooseCharacter(0);
        Player->ServerStartExpedition();
        bStarted = true;
    }
    if (State->Phase != EIslandPhase::Hunting || !Player->IsPlaying() || !Mode->CanPlayerAct(*Player)
        || !Player->IsVoxelInputEnabled() || Player->GetViewTarget() != Player->GetHero()) return;
    AVoxelCharacter* Hero = Player->GetHero();
    AIslandNeutralProp* Ship = nullptr;
    for (TActorIterator<AIslandNeutralProp> It(&World); It; ++It)
        if (It->GetKind() == EIslandNeutralKind::Ship && (!Ship || It->GetActorLocation().X < Ship->GetActorLocation().X)) Ship = *It;
    if (!Ship)
    {
        UE_LOG(LogTemp,Warning,TEXT("WRECK_PREVIEW_NO_SHIP continuing at the normal landing"));
        Destroy();
        return;
    }

    const FTransform& Frame = Ship->GetActorTransform();
    const FVector Across = Ship->GetActorForwardVector();
    const double InlandSide = FVector::DotProduct(Across, -Ship->GetActorLocation()) < 0. ? -1. : 1.;
    const FVector EyeOffset = Hero->GetAimOrigin() - Hero->GetActorLocation();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(WreckPreviewPlank),false,Hero);
    TOptional<FVector> Stand;
    FRotator View = FRotator::ZeroRotator;
    bool bWithinReach = false;
    // Start on a walkable bank by the carved prow. Prefer a spot where the first
    // ordinary shovel input can already hit the painted hull.
    for (double Side : {InlandSide, -InlandSide})
    {
        for (double Along : {-315., -285., -345.})
        {
            const FVector Station = Frame.TransformPosition({0.,Along,0.});
            for (double Distance : {430.,365.,500.,610.,765.})
            {
                const auto Floor = Mode->FindStandingFloor(Hero, Station + Across * (Side * Distance) + FVector(0,0,1200.), 1800.);
                if (!Floor) continue;
                const FVector Eye = *Floor + EyeOffset;
                const FVector Look = (Frame.TransformPosition({0.,Along-22.,165.}) - Eye).GetSafeNormal();
                FHitResult Hit;
                bWithinReach = AIslandNeutralProp::TraceShovel(World,Hit,Eye,Eye + Look * AVoxelGameMode::AimReach,Query)
                    && Hit.GetActor() == Ship;
                if (!Stand || bWithinReach)
                {
                    Stand = Floor;
                    View = bWithinReach ? Hero->GetViewRotationForTarget(Hit.ImpactPoint,*Floor) : Look.Rotation();
                }
                if (bWithinReach) break;
            }
            if (bWithinReach) break;
        }
        if (bWithinReach) break;
    }
    if (Stand)
    {
        Hero->ReturnToStart(FTransform(FRotator(0.,View.Yaw,0.),*Stand));
        Player->ClientResetView(View);
        Player->SetViewTarget(Hero);
        UE_LOG(LogTemp,Display,TEXT("WRECK_PREVIEW_READY pawn=%s ground=%s yaw=%.1f pitch=%.1f shovel_in_reach=%d"),
            *Hero->GetName(),*Stand->ToCompactString(),View.Yaw,View.Pitch,bWithinReach);
        if (FParse::Param(FCommandLine::Get(),TEXT("WreckPreviewFirstDigQA")))
        {
            FTimerHandle Timer;
            Player->GetWorldTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(Player,[Player,Ship]()
            {
                VerifyWreckPreviewInput(*Player,*Ship);
            }),1.f,false);
        }
    }
    else UE_LOG(LogTemp,Warning,TEXT("WRECK_PREVIEW_NO_FLOOR continuing at the normal landing"));
    Destroy();
}

void AExpeditionWalk::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bCrewFilm) { TickCrewFilm(); return; }
    if (bWreckPreview && GetNetMode() == NM_Standalone) { TickWreckPreview(); return; }
    if (bArtTour) { TickArtTour(); return; }
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
        if (bSlopePitRun)
        {
            const FVector2D TowardSea=FVector2D(Dock->GetActorLocation()).GetSafeNormal();
            // Seek a natural inclined near-shore surface, leaving the generated map and its collision untouched.
            for (double Inland=1050.; Inland<=2600.; Inland+=150.)
            {
                const FVector2D Site=FVector2D(Dock->GetActorLocation())-TowardSea*Inland;
                FHitResult Ground;
                if (Terrain->TraceRock(FVector(Site,1400.),FVector(Site,-180.),Hero,Ground)
                    && Ground.ImpactPoint.Z>120. && Ground.ImpactNormal.Z>.91 && Ground.ImpactNormal.Z<.99)
                {
                    Route.Add(Site);
                    EscapeDirection=FVector2D(Ground.ImpactNormal).GetSafeNormal();
                    break;
                }
            }
            if (Route.Num()<2) { StopRun(TEXT("no natural incline in approach")); return; }
        }
        else for (const auto& Circle : Terrain->GetSearchCircles()) Route.Add(FVector2D(Circle.Center));
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
        else if (bSlopePitRun)
        {
            Pit=Position;
            BaselineSequence=Terrain->GetAppliedSequence();
            ChangeStage(EStage::Digging);
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
        if (Pit.Z-Position.Z >= 152. && Hero->GetCharacterMovement()->IsMovingOnGround())
        {
            if (bSlopePitRun)
            {
                const FVector Feet=Position-FVector::UpVector*Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
                FHitResult Floor;
                if (!Terrain->TraceRock(Feet+FVector::UpVector*35.,Feet-FVector::UpVector*60.,Hero,Floor)
                    || Floor.ImpactNormal.Z>=.995 || Floor.ImpactNormal.Z<.7)
                { StopRun(TEXT("measured pit bottom is not an incline")); return; }
                UE_LOG(LogTemp,Display,TEXT("EXPEDITION_SLOPE_FLOOR degrees=%.2f depth=%.1f"),
                    FMath::RadiansToDegrees(FMath::Acos(Floor.ImpactNormal.Z)),Pit.Z-Position.Z);
            }
            PitBottom=Position.Z;
            BaselineSequence=Terrain->GetAppliedSequence();
            SoilThrows=0;
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_WALK_PIT depth=%.1f seq=%u"),Pit.Z-PitBottom,BaselineSequence);
            RecordView(TEXT("05-far-dig"));
            ChangeStage(EStage::Filling);
            return;
        }
        Turn(*Player,FRotator(-83.,Player->GetControlRotation().Yaw,0.),DeltaSeconds);
        if (Now-StageStarted>.6 && Now-LastInput>.59 && !Terrain->IsBusy() && Hero->GetCharacterMovement()->IsMovingOnGround())
        {
            if (auto Point=HighestSupportingRock(*Player,*Terrain))
            {
                Player->SubmitEdit(EVoxelAction::Punch,(*Point-Hero->GetAimOrigin()).GetSafeNormal());
                LastInput=Now;
            }
        }
        return;
    }
    if (Stage==EStage::Filling)
    {
        // Every throw has a visible, colliding flight and a normal authoritative mud landing.
        // Only start walking once the new standing plane actually carries the player to the lip.
        const double Remaining=Pit.Z-Position.Z;
        if (Terrain->GetAppliedSequence()>BaselineSequence && Remaining<=43. && Hero->GetCharacterMovement()->IsMovingOnGround())
        {
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_WALK_MUD_STAIRS throws=%d gain=%.1f remaining=%.1f"),
                SoilThrows,Position.Z-PitBottom,Remaining);
            RecordView(TEXT("06-far-fill"));
            ChangeStage(EStage::Escaping);
            return;
        }
        Turn(*Player,FRotator(-83.,Player->GetControlRotation().Yaw,0.),DeltaSeconds);
        const double Pitch=FRotator::NormalizeAxis(Player->GetControlRotation().Pitch);
        if (Pitch<-75. && Now-LastInput>.85 && !Terrain->IsBusy() && Hero->GetCharacterMovement()->IsMovingOnGround())
        {
            Player->SetExpeditionThrowHeld(true);
            Player->SetExpeditionThrowHeld(false);
            ++SoilThrows;
            LastInput=Now;
        }
        return;
    }
    if (Stage==EStage::Escaping)
    {
        WalkTo(*Player,FVector2D(Pit)+EscapeDirection*340.,DeltaSeconds,20.);
        const double OriginalSurface=VoxelPlayground::BaseHeight(Terrain->Layout,FMath::RoundToInt(Position.X),FMath::RoundToInt(Position.Y));
        const double Feet=Position.Z-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if (FVector::Dist2D(Position,Pit)>165. && FMath::Abs(Feet-OriginalSurface)<35. && Hero->GetCharacterMovement()->IsMovingOnGround())
        {
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_WALK_EXIT surface=1 throws=%d radius=%.1f"),SoilThrows,FVector::Dist2D(Position,Pit));
            if (bSlopePitRun) ChangeStage(EStage::BuildingRoof);
            else
            {
                ChangeStage(EStage::Returning);
                Player->StartRecover();
            }
        }
        return;
    }
    if (Stage==EStage::BuildingRoof)
    {
        if (!bRoofRequested)
        {
            if (Hero->GetVelocity().Size()>5.) return;
            const double Half=Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
            RoofFeet=Position-FVector::UpVector*Half;
            const FVector Support=FVector(-EscapeDirection,0.);
            // Prepare a supported voxel overhang using the same authoritative sand queue as the hopper.
            // Only this optional test builds the fixture; the subsequent throws are the ordinary right input.
            auto* Mode=GetWorld()->GetAuthGameMode<AVoxelGameMode>();
            if (!Mode) { StopRun(TEXT("no authoritative terrain queue")); return; }
            TArray<FVector> Centers;
            for (const FVector2D& Offset : {FVector2D(175.,45.), FVector2D(175.,150.), FVector2D(145.,245.),
                                            FVector2D(75.,277.), FVector2D(0.,277.)})
                Centers.Add(RoofFeet+Support*Offset.X+FVector::UpVector*Offset.Y);
            bRoofRequested=true;
            const TWeakObjectPtr<AExpeditionWalk> Weak(this);
            Mode->EnqueueEnvironmentFill(Centers,[Weak](bool bOK)
            {
                if (auto* Walk=Weak.Get())
                {
                    if (!bOK) Walk->StopRun(TEXT("voxel overhang fixture rejected"));
                    else Walk->bRoofReady=true;
                }
            });
            return;
        }
        if (!bRoofReady || Terrain->IsBusy()) return;
        const FVector Head=Hero->GetActorLocation()+FVector::UpVector*Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        FHitResult Roof;
        if (!Terrain->TraceRock(Head,Head+FVector::UpVector*70.,Hero,Roof))
        { StopRun(TEXT("no colliding roof above the standing capsule")); return; }
        const double Clearance=Roof.ImpactPoint.Z-Head.Z;
        if (Clearance<5. || Clearance>34.) { StopRun(TEXT("overhang does not constrain a 40cm rise")); return; }
        UE_LOG(LogTemp,Display,TEXT("EXPEDITION_MUD_HEADROOM clearance_cm=%.2f sequence=%u"),Clearance,Terrain->GetAppliedSequence());
        BaselineSequence=Terrain->GetAppliedSequence();
        SoilThrows=0;
        ChangeStage(EStage::UnderRoof);
        return;
    }
    if (Stage==EStage::UnderRoof)
    {
        const double FeetZ=Position.Z-Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if (Terrain->GetAppliedSequence()!=BaselineSequence || FMath::Abs(FeetZ-RoofFeet.Z)>5.)
        { StopRun(TEXT("under-ceiling throw embedded or displaced the player")); return; }
        Turn(*Player,FRotator(-83.,Player->GetControlRotation().Yaw,0.),DeltaSeconds);
        if (SoilThrows<2 && FRotator::NormalizeAxis(Player->GetControlRotation().Pitch)<-75. && Now-LastInput>1.)
        {
            Player->SetExpeditionThrowHeld(true);
            Player->SetExpeditionThrowHeld(false);
            TActorIterator<AExpeditionMudBall> Ball(GetWorld());
            bSawThrow|=bool(Ball);
            ++SoilThrows;
            LastInput=Now;
        }
        if (SoilThrows==2 && Now-LastInput>.8)
        {
            if (!bSawThrow) { StopRun(TEXT("right input never produced a projectile")); return; }
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_MUD_HEADROOM_BLOCKED throws=2 seq=%u displacement_cm=%.2f"),BaselineSequence,FeetZ-RoofFeet.Z);
            ChangeStage(EStage::BeyondRoof);
        }
        return;
    }
    if (Stage==EStage::BeyondRoof)
    {
        if (ClearThrowStart.IsZero())
        {
            if (!WalkTo(*Player,FVector2D(RoofFeet)+EscapeDirection*270.,DeltaSeconds,26.)) return;
            if (Hero->GetVelocity().Size()>5.) return;
            ClearThrowStart=Position;
            return;
        }
        if (Terrain->GetAppliedSequence()>BaselineSequence && Position.Z-ClearThrowStart.Z>28.)
        {
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_MUD_HEADROOM_CLEAR rise_cm=%.2f"),Position.Z-ClearThrowStart.Z);
            StopRun({});
            return;
        }
        Turn(*Player,FRotator(-83.,Player->GetControlRotation().Yaw,0.),DeltaSeconds);
        if (FRotator::NormalizeAxis(Player->GetControlRotation().Pitch)<-75. && Now-LastInput>1.)
        {
            Player->SetExpeditionThrowHeld(true);
            Player->SetExpeditionThrowHeld(false);
            LastInput=Now;
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
}
