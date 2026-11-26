#include "Tests/IslandProbe.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Island/ExpeditionDock.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Island/ExpeditionMudBall.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandScenerySupport.h"
#include "Island/IslandSceneryVoxels.h"
#include "Island/IslandPlayerController.h"
#include "Materials/MaterialInterface.h"
#include "UnrealClient.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    constexpr uint8 DigInput = uint8(EVoxelAction::Punch);
    constexpr uint8 ThrowInput = uint8(EVoxelAction::Fill);
    constexpr uint8 RopeInput = 3;
    constexpr uint8 JumpInput = 4;
    const TCHAR* Names[] = {TEXT("Tree"), TEXT("Ship"), TEXT("Hopper"), TEXT("Sail"), TEXT("Tide")};

    FVector Local(const AActor& Actor, double X, double Y, double Z = 0.)
    {
        return Actor.GetActorTransform().TransformPosition(FVector(X,Y,Z));
    }

    double Surface(const AVoxelTerrain& Terrain, const FVector& Point)
    {
        return Terrain.GetDensity()->SurfaceAt(FVector2D(Point)).Get(-10000.);
    }

    FVector BankAim(const FVector& Start,const FVector& Ground)
    {
        // Aim the centre of the travelling sphere a radius above the desired contact.
        const FVector Delta=Ground+FVector::UpVector*AExpeditionMudBall::Radius-Start;
        const double Run=Delta.Size2D();
        if (Run<1.) return Delta.GetSafeNormal();
        const double K=AExpeditionMudBall::FallAcceleration*Run/(2.*FMath::Square(AExpeditionMudBall::ThrowSpeed));
        const double C=Delta.Z/Run-AExpeditionMudBall::ArcLift/AExpeditionMudBall::ThrowSpeed+K;
        const double Discriminant=1.-4.*K*C;
        if (Discriminant<=0.) return FVector::ZeroVector;
        return (Delta.GetSafeNormal2D()+FVector::UpVector*(2.*C/(1.+FMath::Sqrt(Discriminant)))).GetSafeNormal();
    }

    bool CanThrowTo(UWorld& World,const AVoxelCharacter& Hero,const FVector& Start,const FVector& Ground,const AActor* Expected)
    {
        const FVector Aim=BankAim(Start,Ground);
        if (Aim.IsNearlyZero()) return false;
        const FVector Velocity=AExpeditionMudBall::LaunchVelocity(Aim);
        const FCollisionShape Ball=FCollisionShape::MakeSphere(AExpeditionMudBall::Radius);
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(NeutralThrowPath),false,&Hero);
        FVector Previous=Start;
        for (int32 I=1; I<=42; ++I)
        {
            const double T=I/45.;
            const FVector Next=Start+Velocity*T-FVector::UpVector*(.5*AExpeditionMudBall::FallAcceleration*T*T);
            FHitResult Hit;
            if (World.SweepSingleByChannel(Hit,Previous,Next,FQuat::Identity,ECC_Visibility,Ball,Query))
                return Hit.GetActor()==Expected && FVector::Dist(Hit.ImpactPoint,Ground)<24.;
            Previous=Next;
        }
        return false;
    }

    FString Digest(UWorld& World)
    {
        const auto* Terrain = AVoxelTerrain::Find(&World);
        if (!Terrain || !Terrain->IsReady() || Terrain->IsBusy()) return {};
        TArray<FString> Entries;
        for (TActorIterator<AIslandNeutralProp> It(&World); It; ++It)
        {
            const auto& Prop = **It;
            const FVector Pos = Prop.GetActorLocation();
            FString Entry = FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%d"),
                int32(Prop.GetKind()), FMath::RoundToInt(Pos.X), FMath::RoundToInt(Pos.Y), FMath::RoundToInt(Pos.Z),
                FMath::RoundToInt(Prop.GetActorRotation().Yaw), Prop.GetPhase(), Prop.GetPlugMask());
            // Collision observations include the settled trunk, the ship opening, the hopper pile and both mouths.
            for (const auto& XY : {FVector2D(245,0), FVector2D(0,0), FVector2D(0,-150), FVector2D(0,150)})
            {
                const FVector Site = Local(Prop, XY.X, XY.Y);
                FHitResult Hit;
                const bool bHit = World.LineTraceSingleByChannel(Hit, Site + FVector(0,0,620), Site - FVector(0,0,500), ECC_Visibility);
                Entry += FString::Printf(TEXT("/%d:%d"), bHit && Hit.GetActor() == &Prop,
                    bHit ? FMath::RoundToInt(Hit.ImpactPoint.Z) : -10000);
            }
            Entries.Add(Entry);
        }
        Entries.Sort();
        const FString Text = FString::Join(Entries, TEXT("|"));
        return FString::Printf(TEXT("%d:%u:%08x:%08x"), Entries.Num(), Terrain->GetAppliedSequence(),
            Terrain->GetChecksum(), FCrc::StrCrc32(*Text));
    }

    void Photo(const TCHAR* Kind, const TCHAR* State)
    {
        if (!FParse::Param(FCommandLine::Get(), TEXT("IslandScreenshot"))) return;
        FString Dir = FPaths::ScreenShotDir();
        FParse::Value(FCommandLine::Get(), TEXT("FieldShotsDir="), Dir);
        IFileManager::Get().MakeDirectory(*Dir, true);
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("Neutral%s%s.png"),Kind,State)), false, false);
    }

    void FrameWreck(ACameraActor& Camera, const AIslandNeutralProp& Ship, const AVoxelTerrain& Terrain, bool bPassage)
    {
#if WITH_EDITOR
        // Editor-game captures can briefly draw the default material during shader compilation.
        TInlineComponentArray<UProceduralMeshComponent*> Tiles(&Terrain);
        for (UProceduralMeshComponent* Tile : Tiles)
        {
            if (!Tile->GetNumSections()) continue;
            if (UMaterialInterface* Ground = Tile->GetMaterial(0)) Ground->EnsureIsComplete();
            break;
        }
#endif
        FVector BestView = FVector::ZeroVector;
        FVector BestTarget = FVector::ZeroVector;
        double BestScore = -DBL_MAX;
        TActorIterator<ADirectionalLight> SunActor(Ship.GetWorld());
        const FVector Sun = SunActor ? -SunActor->GetActorForwardVector() : FVector::ZeroVector;
        for (double Side : {-1., 1.})
        {
            for (double End : {-1., 1.})
            {
                if (!bPassage && End > 0.) continue;
                FVector View = bPassage ? Local(Ship, Side * 435., End * 38., 95.) : Local(Ship, Side * 960., -652., 625.);
                const FVector Target = bPassage ? Local(Ship, -Side * 65., 0., 42.) : Local(Ship, 0., -72., 185.);
                View.Z = FMath::Max(View.Z, Surface(Terrain, View) + (bPassage ? 88. : 135.));
                // Pick an approach with an open foreground on this seed's sand ridge. On a tie,
                // prefer water behind the silhouette and the second crew visible beyond the tunnel.
                const FVector Beyond = Target + (Target - View).GetSafeNormal2D() * 3800.;
                double Score = FMath::Clamp(Terrain.GetDensity()->ShoreDistance(Beyond.X, Beyond.Y) / 4000., -1., 1.);
                if (bPassage && Side < 0.) Score += .6;
                if (!bPassage)
                {
                    const FVector Eye = Local(Ship, Side * 145., -317., 155.);
                    FHitResult Cover;
                    if (!Terrain.TraceRock(View, Eye, &Ship, Cover)) Score += 5.;
                    Score += FMath::Clamp((Eye.Z - Surface(Terrain, Eye)) / 85., -1., 1.);
                    Score += 8. * FVector::DotProduct(Sun, Ship.GetActorForwardVector() * Side);
                }
                for (double Y : {-275., -125., 0., 125., 275.})
                {
                    const FVector Point = bPassage ? Local(Ship, -Side * 155., Y * .13, 65.) : Local(Ship, Side * 120., Y, 125.);
                    FHitResult Hit;
                    if (!Terrain.TraceRock(View, Point, &Ship, Hit)) Score += 3.;
                }
                if (Score > BestScore)
                {
                    BestScore = Score;
                    BestView = View;
                    BestTarget = Target;
                }
            }
        }
        Camera.SetActorLocationAndRotation(BestView, (BestTarget - BestView).Rotation());
        Camera.GetCameraComponent()->SetFieldOfView(bPassage ? 78.f : 54.f);
    }
}

void AIslandProbePeer::ClientNeutralInput_Implementation(uint8 Action, FVector Target)
{
    auto* Player = CastChecked<AIslandPlayerController>(GetOwner());
    const auto* Hero = Player->GetHero();
    if (!Hero) return;
    if (Action == RopeInput)
    {
        ClientOpenChest_Implementation();
    }
    else if (Action == JumpInput)
    {
        Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::SpaceBar, IE_Pressed, 1.f));
        FTimerHandle Release;
        GetWorldTimerManager().SetTimer(Release, FTimerDelegate::CreateWeakLambda(Player,[Player]
        {
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::SpaceBar, IE_Released, 0.f));
        }), .09f, false);
    }
    else
    {
        // Use the same controller-to-server action, reach, visibility and cooldown as a player's button press.
        const FVector Aim = Action==ThrowInput ? BankAim(Hero->GetAimOrigin(),Target) : (Target - Hero->GetAimOrigin()).GetSafeNormal();
        if (Action == ThrowInput) Player->SubmitExpeditionThrow(Aim);
        else Player->SubmitEdit(EVoxelAction::Punch,Aim);
    }
    UE_LOG(LogTemp, Display, TEXT("NEUTRAL_QA_INPUT team=%d action=%d"), Player->GetTeam(), Action);
}

void AIslandProbePeer::ClientNeutralInspect_Implementation(int32 Stage)
{
    const FString Result = Digest(*GetWorld());
    if (!Result.IsEmpty())
    {
        UE_LOG(LogTemp, Display, TEXT("NEUTRAL_QA_VIEW stage=%d digest=%s"), Stage, *Result);
        ServerNeutralReport(Stage, Result);
    }
}

void AIslandProbePeer::ServerNeutralReport_Implementation(int32 Stage, const FString& Result)
{
    if (auto* Probe = Coordinator.Get()) Probe->RecordNeutral(Seat, Stage, Result);
}

void AIslandProbe::RecordNeutral(int32 Seat, int32 Stage, const FString& Result)
{
    if (RoomCheck == ERoomCheck::Neutral && Stage == NeutralKind * 100 + NeutralStep && Result == NeutralExpected)
        Reports.Add(Seat);
}

void AIslandProbe::StartNeutralCheck(double Now)
{
    Require(Peers.Num() == 3 && ExpeditionTeamForSeat(1) != ExpeditionTeamForSeat(2), TEXT("neutral scenario has rival crews and a camera host"));
    NeutralProps.SetNum(5);
    for (TActorIterator<AIslandNeutralProp> It(GetWorld()); It; ++It)
    {
        const int32 Kind = int32(It->GetKind());
        if (NeutralProps.IsValidIndex(Kind) && (!NeutralProps[Kind].IsValid() || It->GetActorLocation().X < NeutralProps[Kind]->GetActorLocation().X))
            NeutralProps[Kind] = *It;
    }
    for (const auto& Prop : NeutralProps)
        if (!Prop.IsValid()) { Require(false, TEXT("pick a naturally generated all-five seed from NEUTRAL_AUDIT_DONE full_seed")); return; }
    RoomCheck = ERoomCheck::Neutral;
    int32 OnlyKind = 0;
    if (FParse::Value(FCommandLine::Get(),TEXT("NeutralOnlyKind="),OnlyKind))
        NeutralKind = FMath::Clamp(OnlyKind,0,4);
    NeutralCamera = GetWorld()->SpawnActor<ACameraActor>();
    SetNeutralStep(0, Now);
}

void AIslandProbe::SetNeutralStep(int32 Next, double Now)
{
    NeutralStep = Next;
    StepAt = Now;
    LastAction = Now - 1.;
    Reports.Reset();
    NeutralExpected.Reset();
    UE_LOG(LogTemp, Display, TEXT("NEUTRAL_QA_STEP kind=%s step=%d"), Names[NeutralKind], Next);
}

void AIslandProbe::NeutralFrame(const AIslandNeutralProp& Prop)
{
    if (!NeutralCamera.IsValid()) return;
    if (NeutralKind == int32(EIslandNeutralKind::Ship) && FParse::Param(FCommandLine::Get(), TEXT("IslandScreenshot")))
    {
        FrameWreck(*NeutralCamera.Get(), Prop, *Mode->GetTerrain(), NeutralStep == 90);
        auto* Player = Mode->GetPlayers()[0].Get();
        if (AHUD* HUD = Player->GetHUD()) HUD->bShowHUD = false;
        Player->SetViewTarget(NeutralCamera.Get());
        return;
    }
    const FVector Focus[] = {{180,0,144},{0,0,60},{75,0,115},{0,0,145},{0,0,140}};
    const FVector Camera[] = {{-240,-880,480},{670,-500,350},{640,-750,505},{710,210,315},{590,440,440}};
    const bool Upright = NeutralKind == 0 && Prop.GetPhase() < 2;
    const FVector Subject = Prop.GetActorTransform().TransformPosition(Upright ? FVector(0,0,326.) : Focus[NeutralKind]);
    FVector View = Prop.GetActorTransform().TransformPosition(Upright ? FVector(-400,-1340,810.) : Camera[NeutralKind]);
    View.Z = FMath::Max(View.Z, Surface(*Mode->GetTerrain(),View) + 105.);
    NeutralCamera->SetActorLocationAndRotation(View,(Subject-View).Rotation());
    NeutralCamera->GetCameraComponent()->SetFieldOfView(NeutralKind==1 ? 69.f : 55.f);
    Mode->GetPlayers()[0]->SetViewTarget(NeutralCamera.Get());
}

bool AIslandProbe::NeutralFloorPose(int32 Seat, const FVector& Column, FVector& Foot, double TopOffset) const
{
    const auto& Hero = *Mode->GetPlayers()[Seat]->GetHero();
    const auto& Cap = *Hero.GetCapsuleComponent();
    const double HH = Cap.GetScaledCapsuleHalfHeight();
    FHitResult Hit;
    const bool bHit = GetWorld()->SweepSingleByChannel(Hit, Column + FVector(0,0,TopOffset + HH), Column - FVector(0,0,550),
        FQuat::Identity, Cap.GetCollisionObjectType(), FCollisionShape::MakeCapsule(Cap.GetScaledCapsuleRadius(),HH),
        FCollisionQueryParams(SCENE_QUERY_STAT(NeutralStand),false,&Hero), FCollisionResponseParams(Cap.GetCollisionResponseToChannels()));
    if (!bHit || Hit.bStartPenetrating || !Hero.GetCharacterMovement()->IsWalkable(Hit)) return false;
    Foot = Hit.Location - FVector(0,0,HH);
    return true;
}

bool AIslandProbe::NeutralAim(int32 Seat, uint8 Action, const FVector& Target, const AActor* AimActor)
{
    const auto* Hero = Mode->GetPlayers()[Seat]->GetHero();
    const double Head = Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const double Eye = Head + (Hero->GetAimOrigin() - Hero->GetActorLocation()).Z;
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(NeutralSight),false,Hero);
    TOptional<FVector> Spot;
    const bool bFilling = Action == ThrowInput;
    const double Ranges[] = {bFilling ? 125. : 120., bFilling ? 161. : 153., bFilling ? 90. : 85., 170., 195.};
    for (const double Radius : Ranges)
    {
        for (int32 I = 0; I < 24; ++I)
        {
            const FVector Offset = FVector::ForwardVector.RotateAngleAxis(I * 15., FVector::UpVector) * Radius;
            FVector Foot;
            if (!NeutralFloorPose(Seat, Target + Offset, Foot)) continue;
            if (FMath::Abs(Foot.Z - Target.Z) > 215.) continue;
            const FVector EyeAt = Foot + FVector(0,0,Eye+2.);
            const FVector Dir = (Target - EyeAt).GetSafeNormal();
            FHitResult Hit;
            if (!GetWorld()->LineTraceSingleByChannel(Hit,EyeAt,EyeAt+Dir*232.,ECC_Visibility,Query)) continue;
            if (AimActor && Hit.GetActor() != AimActor) continue;
            if (FVector::Dist(Hit.ImpactPoint,Target) > (Cast<AIslandNeutralProp>(AimActor) ? 90. : 48.)) continue;
            if (bFilling && !CanThrowTo(*GetWorld(),*Hero,EyeAt,Target,AimActor)) continue;
            Spot = Foot;
            break;
        }
        if (Spot) break;
    }
    if (!Spot) return false;
    Place(Seat,*Spot + FVector(0,0,Head+3.));
    NeutralPendingSeat = Seat;
    NeutralPendingAction = Action;
    NeutralAimPoint = Target;
    bNeutralWaitingInput = true;
    NeutralSendAt = FPlatformTime::Seconds() + .50;
    return true;
}

void AIslandProbe::AimDecoyRope(AIslandNeutralProp& Sail)
{
    NeutralAim(0, RopeInput, Sail.GetInteractionPoint(), &Sail);
}

void AIslandProbe::NeutralWalk(int32 Seat, const FVector& Start, const FVector& End)
{
    const auto* Hero = Mode->GetPlayers()[Seat]->GetHero();
    NeutralWalkFrom = Start;
    NeutralWalkTo = End;
    NeutralWalkSeat = Seat;
    Place(Seat,Start + FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3.));
    NeutralSendAt = FPlatformTime::Seconds() + .45;
    UE_LOG(LogTemp, Display, TEXT("NEUTRAL_QA_WALK_START kind=%s from=%s to=%s"), Names[NeutralKind], *Start.ToCompactString(), *End.ToCompactString());
}

bool AIslandProbe::NeutralWalkArrived(int32 Seat) const
{
    const auto* Hero = Mode->GetPlayers()[Seat]->GetHero();
    const FVector Foot = Hero->GetActorLocation() - FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    return FVector::Dist2D(Foot,NeutralWalkTo) < 35. && FMath::Abs(Foot.Z-NeutralWalkTo.Z) < 43. &&
        Hero->GetCharacterMovement()->IsMovingOnGround();
}

void AIslandProbe::NeutralDigLane(int32 Seat, const FVector& Point)
{
    // Acquire the visible patch and an actual standing pose for each shallow scoop.
    // After several cuts the original rim can be too high or too narrow for a capsule.
    const auto& Terrain = *Mode->GetTerrain();
    const auto* Hero = Mode->GetPlayers()[Seat]->GetHero();
    const double H = Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const double EyeHeight = H + Hero->GetAimOrigin().Z - Hero->GetActorLocation().Z;
    const FVector Target(Point.X,Point.Y,Surface(Terrain,Point));
    for (double Radius : {0.,22.,47.,72.,102.,143.})
        for (int32 I=0; I<(Radius>0. ? 12 : 1); ++I)
        {
            const FVector XY = Target + FVector::ForwardVector.RotateAngleAxis(30.*I,FVector::UpVector)*Radius;
            const FVector Column(XY.X,XY.Y,Surface(Terrain,XY));
            for (double Height : {80.,22.,350.})
            {
                FVector Foot;
                if (!NeutralFloorPose(Seat,Column,Foot,Height)) continue;
                const FVector Eye=Foot+FVector(0,0,EyeHeight+3.);
                const FVector Line=Target-Eye;
                if (Line.Size()>232.) continue;
                FHitResult Hit;
                if (!Terrain.TraceRock(Eye,Target + Line.GetSafeNormal()*15.,Hero,Hit)
                    || FVector::Dist(Hit.ImpactPoint,Target)>28.) continue;
                Place(Seat,Foot+FVector(0,0,H+3.));
                NeutralAimPoint=Hit.ImpactPoint;
                NeutralPendingSeat=Seat;
                NeutralPendingAction=DigInput;
                NeutralSendAt=FPlatformTime::Seconds()+.30;
                bNeutralWaitingInput=true;
                return;
            }
        }
    UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_LANE_UNREACHABLE site=%s"),*Target.ToCompactString());
}

bool AIslandProbe::NeutralRampPair(const AIslandNeutralProp& Prop, FVector& Start, FVector& End) const
{
    const auto* Terrain = Mode->GetTerrain();
    const auto* Hero = Mode->GetPlayers()[2]->GetHero();
    const auto& Cap = *Hero->GetCapsuleComponent();
    const auto Shape = FCollisionShape::MakeCapsule(Cap.GetScaledCapsuleRadius(),Cap.GetScaledCapsuleHalfHeight());
    const auto Query = FCollisionQueryParams(SCENE_QUERY_STAT(NeutralRampWalk),false,Hero);
    const auto Responses = FCollisionResponseParams(Cap.GetCollisionResponseToChannels());
    const FVector Clearance(0,0,Cap.GetScaledCapsuleHalfHeight()+10.);
    // Walk along the chute, on either shoulder of the poured sand. Cutting across the
    // steep side tests the deliberately obstructed lower road, not the access ramp.
    for (int32 I = 0; I < NeutralSampleLocations.Num(); ++I)
    {
        const FVector Site = NeutralSampleLocations[I];
        const FVector InProp = Prop.GetActorTransform().InverseTransformPosition(Site);
        if (InProp.X < 190. || InProp.X > 350. || FMath::Abs(FMath::Abs(InProp.Y)-48.) > 12.) continue;
        const double Ground = Surface(*Terrain,Site);
        if (Ground - NeutralSurfaceBefore[I] < 35.) continue;
        FVector High;
        if (!NeutralFloorPose(2,Site,High) || FMath::Abs(High.Z-Ground) > 28.) continue;
        FVector Last = High;
        for (int32 Slice = 1; Slice <= FMath::CeilToInt(10.*Prop.GetActorScale3D().X); ++Slice)
        {
            const FVector Column = Site - Prop.GetActorForwardVector() * (24.*Slice);
            FVector Point;
            if (!NeutralFloorPose(2,Column,Point) || FMath::Abs(Point.Z-Surface(*Terrain,Column)) > 30.) break;
            const double StepRise = Last.Z - Point.Z;
            if (StepRise < -11. || StepRise > 22.) break;
            FHitResult Hit;
            if (GetWorld()->SweepSingleByChannel(Hit,Last+Clearance,Point+Clearance,FQuat::Identity,
                    Cap.GetCollisionObjectType(),Shape,Query,Responses)) break;
            if (Slice >= 12 && High.Z-Point.Z >= 82.)
            {
                Start = Point;
                End = High;
                return true;
            }
            Last = Point;
        }
    }
    return false;
}

void AIslandProbe::TickNeutralCheck(double Now)
{
    if (FParse::Param(FCommandLine::Get(),TEXT("NeutralDemolitionQA")))
    {
        TickSceneryCheck(Now);
        return;
    }
    const double Age = Now-StepAt;
    auto& Prop = *NeutralProps[NeutralKind].Get();
    auto& Terrain = *Mode->GetTerrain();
    const auto& Players = Mode->GetPlayers();
    // At 40 cm per scoop it takes several tool cycles to prepare each mouth of a real walkable tunnel.
    const bool bExcavating = (NeutralKind==0 && (NeutralStep==5 || NeutralStep==6))
        || (NeutralKind==1 && (NeutralStep==3 || NeutralStep==4));
    if (Age > (bExcavating ? 80. : 24.))
    {
        UE_LOG(LogTemp,Error,TEXT("ISLAND_QA_TIMEOUT neutral=%s step=%d phase=%d mask=%d stroke=%d seq=%u expected=%s reports=%d actor1=%s actor2=%s target=%s"),
            Names[NeutralKind],NeutralStep,Prop.GetPhase(),Prop.GetPlugMask(),NeutralStroke,Terrain.GetAppliedSequence(),*NeutralExpected,Reports.Num(),
            *Players[1]->GetHero()->GetActorLocation().ToCompactString(), *Players[2]->GetHero()->GetActorLocation().ToCompactString(),*NeutralAimPoint.ToCompactString());
        Require(false,TEXT("neutral input or observation did not complete"));
        return;
    }
    if (bNeutralWaitingInput)
    {
        if (Now >= NeutralSendAt)
        {
            Peers[NeutralPendingSeat]->ClientNeutralInput(NeutralPendingAction,NeutralAimPoint);
            bNeutralWaitingInput=false;
            LastAction=Now;
        }
        return;
    }
    const auto Next = [&] { SetNeutralStep(NeutralStep+1,Now); };
    const auto Done = [&]
    {
        SetNeutralStep(90,Now);
        if (NeutralKind == int32(EIslandNeutralKind::Ship)) NeutralFrame(Prop);
    };
    const auto Capture = [&](const TCHAR* When) { NeutralFrame(Prop); Photo(Names[NeutralKind],When); };
    const auto Aim = [&](int32 Seat,uint8 Act,const FVector& P,const AActor* Target)
    {
        if (Now-LastAction>.8)
        {
            if (!NeutralAim(Seat,Act,P,Target))
                UE_LOG(LogTemp,Verbose,TEXT("NEUTRAL_QA_NO_STAND site=%s action=%u"),*P.ToCompactString(),Act);
            LastAction=Now;
        }
    };
    const auto TryWalk = [&]()
    {
        if (Now>=NeutralSendAt)
        {
            Peers[NeutralWalkSeat]->ClientWalkToDig(NeutralWalkTo);
            NeutralSendAt=Now+1.0;
        }
        return Age>.38 && NeutralWalkArrived(NeutralWalkSeat);
    };
    const auto Shared = [&]()
    {
        if (Age<.7 || Terrain.IsBusy()) return false;
        if (NeutralExpected.IsEmpty()) NeutralExpected=Digest(*GetWorld());
        if (Now-LastAction>.32)
        {
            for (int32 Seat=0; Seat<Peers.Num(); ++Seat)
                if (!Reports.Contains(Seat)) Peers[Seat]->ClientNeutralInspect(NeutralKind*100+NeutralStep);
            LastAction=Now;
        }
        if (Reports.Num()!=Peers.Num()) return false;
        if (NeutralStep!=1 && Prop.GetKind()!=EIslandNeutralKind::Ship)
        {
            // Validate the actual lowered sail, overturned trunk and moving gate at their replicated transforms.
            TMap<UIslandMeshVoxels*,TArray<int32>> Loose;
            IslandScenerySupport::InspectActor(Prop,Terrain,Loose);
            for (const auto& Loss : Loose)
                if (Loss.Value.Num()==Loss.Key->RemainingCount())
                {
                    Require(false,*FString::Printf(TEXT("%s phase %d has a detached %s"), Names[NeutralKind],Prop.GetPhase(),
                        *GetNameSafe(Loss.Key->SourcePart())));
                    return false;
                }
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_POSE_SUPPORTED kind=%s phase=%d step=%d"),Names[NeutralKind],Prop.GetPhase(),NeutralStep);
        }
        return true;
    };
    if (NeutralStep==0)
    {
        for (int32 Seat : {1,2}) Mode->RecoverPlayer(*Players[Seat]);
        NeutralSampleLocations.Reset();
        NeutralSurfaceBefore.Reset();
        NeutralStroke=0;
        NeutralFrame(Prop);
        Next();
        return;
    }
    if (NeutralStep==1 || NeutralStep==90)
    {
        if (NeutralKind == int32(EIslandNeutralKind::Ship) && FParse::Param(FCommandLine::Get(), TEXT("IslandScreenshot")) && Age < 2.) return;
        if (!Shared()) return;
        UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_SHARED kind=%s state=%s peers=%d digest=%s"),Names[NeutralKind],
            NeutralStep==1?TEXT("before"):TEXT("after"),Reports.Num(),*NeutralExpected);
        if (NeutralStep==1)
        {
            Capture(TEXT("Before"));
            Next();
        }
        else
        {
            if (NeutralKind==0 || NeutralKind==1) Capture(TEXT("After"));
            Next();
        }
        return;
    }
    if (NeutralStep==91)
    {
        if (Age<.3) return;
        if (NeutralKind == int32(EIslandNeutralKind::Ship) && FParse::Param(FCommandLine::Get(), TEXT("IslandScreenshot")))
            if (AHUD* HUD = Players[0]->GetHUD()) HUD->bShowHUD = true;
        ++NeutralKind;
        int32 OnlyKind = 0;
        const bool bSingle = FParse::Value(FCommandLine::Get(),TEXT("NeutralOnlyKind="),OnlyKind);
        if (NeutralKind==5 || bSingle)
        {
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_DONE players=3 teams=2 kinds=%d"), bSingle ? 1 : 5);
            Advance(EStep::Done);
            return;
        }
        SetNeutralStep(0,Now);
        return;
    }

    if (NeutralKind==0)
    {
        // Tree. First prove the fallen shape really blocks a character at the old ground
        // level. The other crew then excavates both flanks and crosses under it on foot.
        if (NeutralStep==2 || NeutralStep==3)
        {
            if (Prop.GetPhase()>=NeutralStep-1) { Next(); return; }
            Aim(1,DigInput,Prop.GetInteractionPoint(),&Prop);
            return;
        }
        if (NeutralStep==4)
        {
            Mode->RecoverPlayer(*Players[1]);
            if (Prop.GetPhase()!=3) return;
            const FVector C = Local(Prop,248,0);
            NeutralBase = Surface(Terrain,C);
            FHitResult Hit;
            const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit,Local(Prop,248,-168,110.),Local(Prop,248,168,110.),ECC_Visibility);
            Require(bBlocked && Hit.GetActor()==&Prop, TEXT("the settled trunk spans the route at character height"));
            const FVector Far = Local(Prop,440.,0.,90.);
            const bool bOuter = GetWorld()->LineTraceSingleByChannel(Hit, Far-Prop.GetActorRightVector()*300.,
                Far+Prop.GetActorRightVector()*300., ECC_Visibility) && Hit.GetActor() == &Prop;
            Require(bOuter, TEXT("fallen tree blocks the outer crossing over thirteen metres from its root"));
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_RANGE tree_outer_cm=%.0f"),FVector::Dist2D(Far,Prop.GetActorLocation()));
            Next();
            return;
        }
        if (NeutralStep==5)
        {
            if (Terrain.IsBusy() || Now-LastAction<.63) return;
            const auto* Hero=Players[2]->GetHero();
            const double TrunkZ=Prop.GetActorLocation().Z;
            const double LaneX=248.;
            const FVector SiteScale=Prop.GetActorScale3D();
            const double TunnelEntranceY=(46.*SiteScale.Y+84.)/SiteScale.Y;
            const double Bed=FMath::Min(NeutralBase-65.,TrunkZ+(90.-46.)*SiteScale.Z
                -2.*Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-26.);
            // Scan below the wood. A shaft can retain an undisturbed roof above its side
            // opening, so the uppermost density crossing is not its tunnel floor.
            auto Below = [&](double X,double Y,FVector& Out)
            {
                const FVector Site=Local(Prop,X,Y);
                const auto& Cap=*Hero->GetCapsuleComponent();
                const double H=Cap.GetScaledCapsuleHalfHeight();
                for (double FootZ=Bed-TrunkZ+22.; FootZ>=Bed-TrunkZ-192.; FootZ-=12.)
                {
                    FHitResult Hit;
                    const FVector C(Site.X,Site.Y,TrunkZ+FootZ+H);
                    if (!GetWorld()->SweepSingleByChannel(Hit,C,C-FVector(0,0,120),FQuat::Identity,Cap.GetCollisionObjectType(),
                        FCollisionShape::MakeCapsule(Cap.GetScaledCapsuleRadius(),H),
                        FCollisionQueryParams(SCENE_QUERY_STAT(NeutralTunnel),false,Hero),FCollisionResponseParams(Cap.GetCollisionResponseToChannels()))
                        || Hit.bStartPenetrating || Hit.GetActor()!=&Terrain || !Hero->GetCharacterMovement()->IsWalkable(Hit)) continue;
                    Out=Hit.Location-FVector(0,0,H);
                    return true;
                }
                return false;
            };
            FVector Stand[2];
            for (int32 I=0; I<2; ++I)
            {
                const double Y = I ? TunnelEntranceY : -TunnelEntranceY;
                const FVector Mouth = Local(Prop,LaneX,Y);
                if (Surface(Terrain,Mouth) > Bed || !Below(LaneX,Y,Stand[I]) || Stand[I].Z > Bed+20.)
                {
                    NeutralDigLane(2,Mouth);
                    ++NeutralStroke;
                    LastAction=Now;
                    return;
                }
            }

            // Excavate only the voxels in the standing corridor, including both shoulders.
            // A ray that meets no soil before the chosen sample must not drill the opposite bank.
            const auto& Density=*Terrain.GetDensity();
            bool bRockRemains=false;
            for (double Y=-TunnelEntranceY+12./SiteScale.Y; Y<TunnelEntranceY; Y+=21./SiteScale.Y)
                for (double X=LaneX-49./SiteScale.X; X<=LaneX+50./SiteScale.X; X+=16./SiteScale.X)
                    for (double AboveBed=19.; AboveBed<=177.; AboveBed+=20.)
                    {
                        const FVector P=Local(Prop,X,Y)+FVector(0,0,Bed-TrunkZ+AboveBed);
                        const FVector Grid=(P-FVector(Density.Grid.Origin))/VoxelPlayground::Spacing;
                        const FIntVector Sample(FMath::RoundToInt(Grid.X),FMath::RoundToInt(Grid.Y),FMath::RoundToInt(Grid.Z));
                        if (Density.Sample(Sample)>0) continue;
                        bRockRemains=true;
                        const FVector Cell=FVector(Density.Grid.Origin+Sample*VoxelPlayground::Spacing);
                        for (int32 Side : {Y<0.?0:1,Y<0.?1:0})
                        {
                            const FVector Eye=Stand[Side]+FVector(0,0,Hero->GetAimOrigin().Z-Hero->GetActorLocation().Z+Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3.);
                            const FVector Ray=Cell-Eye;
                            const FVector Dir=Ray.GetSafeNormal();
                            FHitResult Hit;
                            if (!Terrain.TraceRock(Eye,Eye+Dir*FMath::Min(228.,Ray.Size()+12.),Hero,Hit)) continue;
                            const FVector At=Prop.GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
                            if (FMath::Abs(At.Y)>TunnelEntranceY+9. || Hit.ImpactPoint.Z<Bed-12.) continue;
                            Place(2,Stand[Side]+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3.));
                            NeutralAimPoint=Hit.ImpactPoint;
                            NeutralPendingSeat=2;
                            NeutralPendingAction=DigInput;
                            bNeutralWaitingInput=true;
                            NeutralSendAt=Now+.30;
                            ++NeutralStroke;
                            LastAction=Now;
                            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_TUNNEL_PUNCH hit=%s cell=%s"),*At.ToCompactString(),*Prop.GetActorTransform().InverseTransformPosition(Cell).ToCompactString());
                            return;
                        }
                    }
            // The two measured ground poses are the only placements. The crossing itself uses normal movement.
            NeutralWalk(2,Stand[0],Stand[1]);
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_TUNNEL_FLOORS near=%.0f far=%.0f residual=%d"),Stand[0].Z-TrunkZ,Stand[1].Z-TrunkZ,bRockRemains);
            Next();
            return;
        }
        if (NeutralStep==6)
        {
            TryWalk();
            const auto* Hero=Players[2]->GetHero();
            const FVector Foot=Hero->GetActorLocation()-FVector(0,0,90.);
            const FVector Rel=Prop.GetActorTransform().InverseTransformPosition(Foot);
            const FVector End=Prop.GetActorTransform().InverseTransformPosition(NeutralWalkTo);
            if (Age>.7 && Rel.Y*FMath::Sign(End.Y)>FMath::Abs(End.Y)-30./Prop.GetActorScale3D().Y &&
                FVector::Dist2D(Foot,NeutralWalkTo)<48. && NeutralBase-Foot.Z>40. &&
                Hero->GetCharacterMovement()->IsMovingOnGround())
            {
                UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_TREE_CROSS team=1 distance=%.0f stroke=%d depth=%.0f"),
                    FVector::Dist2D(NeutralWalkFrom,Foot),NeutralStroke,NeutralBase-Foot.Z);
                Done();
            }
            else if (Age>1.35 && Now-LastAction>1.15)
            {
                // If the lip of a cut is still in the way, the walking guest carves it
                // in place. The server again checks the real target and normal reach.
                const FVector Eye=Hero->GetAimOrigin();
                const FVector Flat = (NeutralWalkTo-Foot).GetSafeNormal2D();
                const FVector Across = FVector::CrossProduct(Flat,FVector::UpVector);
                bool bSent = false;
                const UCapsuleComponent& Capsule = *Hero->GetCapsuleComponent();
                const FVector Body = Hero->GetActorLocation() + FVector::UpVector * 5.;
                const FCollisionQueryParams Query(SCENE_QUERY_STAT(NeutralTunnelObstruction),false,Hero);
                FHitResult Block, Face;
                if (GetWorld()->SweepSingleByChannel(Block,Body,Body+Flat*85.,FQuat::Identity,Capsule.GetCollisionObjectType(),
                    FCollisionShape::MakeCapsule(Capsule.GetScaledCapsuleRadius(),Capsule.GetScaledCapsuleHalfHeight()),
                    Query,FCollisionResponseParams(Capsule.GetCollisionResponseToChannels())))
                {
                    // A short ray at eye level can miss the lip actually touching the guest's knee.
                    const FVector Wanted = Block.GetActor()==&Prop ? Foot+Flat*15. : Block.ImpactPoint-Block.ImpactNormal*5.;
                    const FVector Dir = (Wanted-Eye).GetSafeNormal();
                    if (Terrain.TraceRock(Eye,Eye+Dir*228.,Hero,Face))
                    {
                        Peers[2]->ClientNeutralInput(DigInput,Face.ImpactPoint);
                        ++NeutralStroke;
                        bSent=true;
                    }
                }
                for (double Distance : {54.,90.,145.})
                {
                    if (bSent) break;
                    for (double Side : {0.,-26.,26.})
                    {
                        for (double AboveFloor : {42.,76.,111.,151.,15.})
                        {
                            const FVector Wall = Foot + Flat*Distance + Across*Side + FVector(0,0,AboveFloor);
                            const FVector Dir = (Wall-Eye).GetSafeNormal();
                            FHitResult Hit;
                            if (!Terrain.TraceRock(Eye,Eye+Dir*229.,Hero,Hit)) continue;
                            const double Advance = FVector::DotProduct(Hit.ImpactPoint-Foot,Flat);
                            const FVector At = Prop.GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
                            // Excavate the wall or a high lip ahead, preserving the ground under our own feet.
                            if (Advance<15. || Advance>145. || FMath::Abs(At.Y)>130. || Hit.ImpactPoint.Z<Foot.Z-12.) continue;
                            Peers[2]->ClientNeutralInput(DigInput,Hit.ImpactPoint);
                            ++NeutralStroke;
                            bSent=true;
                            break;
                        }
                        if (bSent) break;
                    }
                    if (bSent) break;
                }
                UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_CROSS_PROGRESS local=%s targetY=%.0f"),*Rel.ToCompactString(),End.Y);
                LastAction=Now;
            }
        }
        return;
    }

    if (NeutralKind==1)
    {
        const FVector Center=Local(Prop,0,0);
        if (NeutralStep==2)
        {
            FHitResult Port, Starboard;
            auto EndCap = [&](double Y, FHitResult& Hit)
            {
                return GetWorld()->LineTraceSingleByChannel(Hit,Local(Prop,0,Y,500.),Local(Prop,0,Y,-50.),ECC_Visibility)
                    && Hit.GetActor() == &Prop;
            };
            Require(EndCap(-365.,Port) && EndCap(365.,Starboard),TEXT("ship bow and stern retain solid collision at full length"));
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_RANGE ship_solid_span_cm=%.0f"),FVector::Dist2D(Port.ImpactPoint,Starboard.ImpactPoint));
            NeutralBase=Surface(Terrain,Center);
            Next();
            return;
        }
        if (NeutralStep==3)
        {
            if (Terrain.IsBusy() || Now-LastAction<.68) return;
            const double Outer = 240.;
            const auto* Hero=Players[1]->GetHero();
            const auto& Capsule=*Hero->GetCapsuleComponent();
            const double Half=Capsule.GetScaledCapsuleHalfHeight();
            const double Bed=Local(Prop,0,0,116.5).Z-2.*Half-33.;
            // Revisit each clear vertical sight line until it reaches the working level.
            // The gunport lintels are solid; excavate beneath them from the sides below.
            for (const double X : {-Outer,Outer,-113.,-67.,-25.,19.,67.,113.})
            {
                const FVector P = Local(Prop,X,0);
                if (Surface(Terrain,P) <= Bed+18.) continue;
                NeutralDigLane(1,P);
                ++NeutralStroke;
                LastAction=Now;
                return;
            }
            // The lintels cast a shadow over both mouths. Sampling only the top of the sand
            // misses a narrow knee- or shoulder-high plug under a perfectly clear centre ray.
            const double EyeHeight=Half+Hero->GetAimOrigin().Z-Hero->GetActorLocation().Z;
            const auto& Density=*Terrain.GetDensity();
            TArray<FVector,TInlineAllocator<12>> Stands;
            for (double X : {-Outer,Outer,0.,-110.,110.,-67.,67.,-194.,194.})
            {
                const FVector Site=Local(Prop,X,0.);
                const FVector Ground(Site.X,Site.Y,Surface(Terrain,Site));
                FVector Foot;
                for (double Height : {22.,75.,350.})
                    if (NeutralFloorPose(1,Ground,Foot,Height) && FMath::Abs(Foot.Z-Ground.Z)<42.)
                    { Stands.Add(Foot); break; }
            }
            const double LaneRadius=(Capsule.GetScaledCapsuleRadius()+12.)/Prop.GetActorScale3D().Y;
            for (double X=-221.; X<=222.; X+=25./Prop.GetActorScale3D().X)
                for (double Y=-LaneRadius; Y<=LaneRadius; Y+=16./Prop.GetActorScale3D().Y)
                    for (double Height=18.; Height<180.; Height+=20.)
                    {
                        const FVector P=Local(Prop,X,Y,Bed-Prop.GetActorLocation().Z+Height);
                        const FVector Grid=(P-FVector(Density.Grid.Origin))/VoxelPlayground::Spacing;
                        const FIntVector Sample(FMath::RoundToInt(Grid.X),FMath::RoundToInt(Grid.Y),FMath::RoundToInt(Grid.Z));
                        if (Density.Sample(Sample)>0) continue;
                        const FVector Cell(Density.Grid.Origin+Sample*VoxelPlayground::Spacing);
                        for (const FVector& Stand : Stands)
                        {
                            const FVector Eye=Stand+FVector(0,0,EyeHeight+3.);
                            const FVector Ray=Cell-Eye;
                            const FVector Dir=Ray.GetSafeNormal();
                            FHitResult Hit;
                            if (!Terrain.TraceRock(Eye,Eye+Dir*FMath::Min(228.,Ray.Size()+12.),Hero,Hit)) continue;
                            const FVector At=Prop.GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
                            if (FMath::Abs(At.X)>248. || FMath::Abs(At.Y)>72. || Hit.ImpactPoint.Z<Bed-15.) continue;
                            Place(1,Stand+FVector(0,0,Half+3.));
                            NeutralPendingSeat=1;
                            NeutralPendingAction=DigInput;
                            NeutralAimPoint=Hit.ImpactPoint;
                            bNeutralWaitingInput=true;
                            NeutralSendAt=Now+.28;
                            LastAction=Now;
                            ++NeutralStroke;
                            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_SHIP_CUT hit=%s cell=%s"),*At.ToCompactString(),*Prop.GetActorTransform().InverseTransformPosition(Cell).ToCompactString());
                            return;
                        }
                    }
            if (NeutralBase-Surface(Terrain,Center)<75.)
            {
                NeutralDigLane(1,Center);
                LastAction=Now;
                return;
            }
            // A shallow scoop can leave the lip at the nominal coordinate too steep to stand on.
            // Resolve both ends on the actual sand outside the gunports, as a player would.
            TOptional<FVector> Ends[2];
            for (int32 I=0; I<2; ++I)
                for (double X : {240.,227.,255.,271.,290.,315.})
                    for (double Y : {0.,17.,-17.,32.,-32.})
                    {
                        if (Ends[I]) continue;
                        const FVector Site=Local(Prop,(I?1.:-1.)*X,Y);
                        const FVector Ground(Site.X,Site.Y,Surface(Terrain,Site));
                        FVector Foot;
                        for (double Height : {26.,70.,180.})
                            if (NeutralFloorPose(2,Ground,Foot,Height) && FMath::Abs(Foot.Z-Ground.Z)<42.)
                            { Ends[I]=Foot; break; }
                    }
            if (Ends[0] && Ends[1])
            {
                NeutralWalk(2,*Ends[0],*Ends[1]);
                Next();
            }
            else if (Now-LastAction>1.)
            {
                UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_SHIP_ENDPOINTS near=%d far=%d depth=%.0f"),
                    Ends[0].IsSet(),Ends[1].IsSet(),NeutralBase-Surface(Terrain,Center));
                LastAction=Now;
            }
            return;
        }
        if (NeutralStep==4)
        {
            TryWalk();
            const auto& Guest = *Players[2]->GetHero();
            const FVector Foot = Guest.GetActorLocation() - FVector(0,0,Guest.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            const FVector At = Prop.GetActorTransform().InverseTransformPosition(Foot);
            // A side passage ends beyond the outer edge of the gunport (+184cm).
            // A fresh cut can lower the far bank after the walk began.
            if (At.X < 217. || FMath::Abs(At.Y) > 57. || !Guest.GetCharacterMovement()->IsMovingOnGround())
            {
                if (Age > 1.4 && Now-LastAction > .95 && !Terrain.IsBusy())
                {
                    const FVector Eye=Guest.GetAimOrigin();
                    const FVector Forward=Prop.GetActorForwardVector();
                    const auto& Cap=*Guest.GetCapsuleComponent();
                    const auto Shape=FCollisionShape::MakeCapsule(Cap.GetScaledCapsuleRadius(),Cap.GetScaledCapsuleHalfHeight());
                    const auto Query=FCollisionQueryParams(SCENE_QUERY_STAT(NeutralShipObstruction),false,&Guest);
                    const auto Responses=FCollisionResponseParams(Cap.GetCollisionResponseToChannels());
                    const FVector From=Guest.GetActorLocation()+FVector(0,0,4.);
                    FHitResult Block,Face;
                    bool bDug=false;
                    if (GetWorld()->SweepSingleByChannel(Block,From,From+Forward*86.,FQuat::Identity,Cap.GetCollisionObjectType(),Shape,Query,Responses))
                    {
                        const FVector Wanted=Block.GetActor()==&Prop ? Foot+Forward*15. : Block.ImpactPoint-Block.ImpactNormal*6.;
                        const FVector Dir=(Wanted-Eye).GetSafeNormal();
                        if (Terrain.TraceRock(Eye,Eye+Dir*228.,&Guest,Face))
                        {
                            Peers[2]->ClientNeutralInput(DigInput,Face.ImpactPoint);
                            bDug=true;
                            ++NeutralStroke;
                        }
                    }
                    // If the remaining obstruction is a small turf lip, a regular jump can step onto it.
                    if (!bDug || NeutralStroke%4==0) Peers[2]->ClientNeutralInput(JumpInput,NeutralWalkTo);
                    UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_SHIP_PROGRESS foot=%s"),*At.ToCompactString());
                    LastAction=Now;
                }
                return;
            }
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_SHIP_PASS team=1 distance=%.0f depth=%.0f"),
                FVector::Dist2D(NeutralWalkFrom,Foot),NeutralBase-Surface(Terrain,Center));
            Mode->RecoverPlayer(*Players[2]);
            // Build a bank against the open edge of the deck and verify it supports the capsule.
            NeutralSampleLocations.Reset();
            NeutralSurfaceBefore.Reset();
            for (double Y : {155.,185.,215.,245.})
                for (double X : {195.,220.,245.,270.,295.})
                {
                    const FVector P=Local(Prop,X,Y);
                    NeutralSampleLocations.Add(P);
                    NeutralSurfaceBefore.Add(Surface(Terrain,P));
                }
            Next();
            return;
        }
        if (NeutralStep==5)
        {
            if (Terrain.IsBusy()) return;
            for (int32 I=0; I<NeutralSampleLocations.Num(); ++I)
            {
                const FVector P=NeutralSampleLocations[I];
                if (Surface(Terrain,P)-NeutralSurfaceBefore[I]<40.) continue;
                FVector Foot;
                if (!NeutralFloorPose(1,P,Foot) || FMath::Abs(Foot.Z-Surface(Terrain,P))>40.) continue;
                // Use the actual capsule floor on an intact deck plank. Approach from
                // the outer sand bank with ordinary walking/jumping and inspect the landing.
                FVector Deck;
                if (!NeutralFloorPose(1,Local(Prop,98,177),Deck)) continue;
                const double PlankHeight = Deck.Z - Prop.GetActorLocation().Z;
                if (Deck.Z-Foot.Z > 120. || PlankHeight < 148. || PlankHeight > 164.) continue;
                UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_SHIP_BANK rise=%.0f"),Surface(Terrain,P)-NeutralSurfaceBefore[I]);
                NeutralWalk(1,Foot,Deck);
                NeutralStroke=0;
                Next();
                return;
            }
            const FVector P=Local(Prop,229,190);
            Aim(1,ThrowInput,FVector(P.X,P.Y,Surface(Terrain,P)),&Terrain);
        }
        if (NeutralStep==6)
        {
            const auto* Hero=Players[1]->GetHero();
            const auto& Movement = *Hero->GetCharacterMovement();
            TryWalk();
            const FHitResult& Floor = Movement.CurrentFloor.HitResult;
            const FVector Foot = Hero->GetActorLocation()-FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            const FVector OnShip = Prop.GetActorTransform().InverseTransformPosition(Floor.ImpactPoint);
            const bool OnPlank = Movement.IsMovingOnGround() && Floor.GetActor()==&Prop &&
                Floor.ImpactNormal.Z>.85 && OnShip.Z>149. && OnShip.Z<160. &&
                FMath::Abs(OnShip.X)<149. && OnShip.Y>150. && OnShip.Y<205. &&
                FMath::Abs(Foot.Z-Floor.ImpactPoint.Z)<9.;
            if (OnPlank && FVector::Dist2D(Foot,NeutralWalkTo)<25.)
            {
                UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_SHIP_DECK team=0 height=%.0f"),OnShip.Z);
                Done();
            }
            else if (Age>.74+NeutralStroke*.8 && Movement.IsMovingOnGround() && Foot.Z < NeutralWalkTo.Z-22.)
            {
                Peers[1]->ClientNeutralInput(JumpInput,NeutralWalkTo);
                ++NeutralStroke;
            }
        }
        return;
    }

    if (NeutralKind==2)
    {
        if (NeutralStep==2)
        {
            for (double X=-60; X<=400; X+=16)
                for (double Y=-80;Y<=80;Y+=16)
                {
                    const FVector P=Local(Prop,X,Y);
                    NeutralSampleLocations.Add(P);
                    NeutralSurfaceBefore.Add(Surface(Terrain,P));
                }
            Next();
            return;
        }
        if (NeutralStep==3)
        {
            if (Prop.GetPhase()>=1) { Mode->RecoverPlayer(*Players[1]); Next(); return; }
            Aim(1,DigInput,Prop.GetInteractionPoint(),&Prop);
            return;
        }
        if (NeutralStep==4)
        {
            if (Prop.GetPhase()!=2 || Terrain.IsBusy()) return;
            FVector A,B;
            if (!NeutralRampPair(Prop,A,B)) return;
            FBox2D Changed(ForceInit);
            for (int32 I=0; I<NeutralSampleLocations.Num(); ++I)
            {
                const FVector& P = NeutralSampleLocations[I];
                if (Surface(Terrain,P) > NeutralSurfaceBefore[I]+12.)
                {
                    const FVector Delta = P - Prop.GetActorLocation();
                    Changed += FVector2D(FVector::DotProduct(Delta,Prop.GetActorForwardVector()),FVector::DotProduct(Delta,Prop.GetActorRightVector()));
                }
            }
            Require(Changed.bIsValid && Changed.GetSize().X > 400. && Changed.GetSize().Y > 260.,TEXT("released sand changes several metres of both lanes"));
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_RANGE hopper_fill_length_cm=%.0f hopper_fill_width_cm=%.0f"),Changed.GetSize().X,Changed.GetSize().Y);
            NeutralWalk(2,A,B);
            Next();
            return;
        }
        if (NeutralStep==5 && TryWalk())
        {
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_HOPPER_ASCENT team=1 distance=%.0f rise=%.0f"),
                FVector::Dist2D(NeutralWalkFrom,NeutralWalkTo),NeutralWalkTo.Z-NeutralWalkFrom.Z);
            Capture(TEXT("After"));
            NeutralBase=Surface(Terrain,NeutralWalkTo);
            Next();
            return;
        }
        if (NeutralStep==6)
        {
            const double Change=NeutralBase-Surface(Terrain,NeutralWalkTo);
            if (Change>55.)
            {
                UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_HOPPER_CUT drop=%.0f"),Change);
                Done();
            }
            else if (Now-LastAction>.85) { NeutralDigLane(2,NeutralWalkTo); LastAction=Now; }
        }
        return;
    }

    if (NeutralKind==3)
    {
        const bool bScreensLane = AIslandNeutralProp::AnySailObscures(*GetWorld(),
            Local(Prop,-100.,180.,110.),Local(Prop,100.,180.,110.));
        if (NeutralStep==2)
        {
            if (Prop.GetPhase()==1) { Next(); return; }
            Aim(1,RopeInput,Prop.GetInteractionPoint(),&Prop);
            return;
        }
        if (NeutralStep==3 && Shared())
        {
            Require(bScreensLane,TEXT("lowered sail hides a crew across the outer half of the fork"));
            if (!bScreensLane) return;
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_SHARED kind=Sail state=active peers=3 digest=%s"),*NeutralExpected);
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_RANGE sail_sight_offset_cm=%.0f"),FVector::Dist2D(Local(Prop,0,180.),Prop.GetActorLocation()));
            Capture(TEXT("After"));
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_SAIL lowered_by=0"));
            Next();
            return;
        }
        if (NeutralStep==4)
        {
            if (Prop.GetPhase()==0)
            {
                if (bScreensLane) return;
                UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_SAIL raised_by=1"));
                Done();
                return;
            }
            Aim(2,RopeInput,Prop.GetInteractionPoint(),&Prop);
        }
        return;
    }

    if (NeutralKind==4)
    {
        const FVector A=Prop.GetTideMouth(0), B=Prop.GetTideMouth(1);
        if (NeutralStep==2)
        {
            if (Prop.GetPlugMask()==1)
            {
                Mode->RecoverPlayer(*Players[1]);
                FVector Foot;
                bool bFound = false;
                for (const FVector Dir : {Prop.GetActorRightVector(), -Prop.GetActorRightVector(), Prop.GetActorForwardVector(), -Prop.GetActorForwardVector()})
                {
                    if (NeutralFloorPose(2,B+Dir*250.,Foot) && FMath::Abs(B.Z-Foot.Z)<65.) { bFound=true; break; }
                }
                if (!bFound) return;
                NeutralWalkFrom=Foot;
                Place(2,Foot+FVector(0,0,93));
                NeutralBase=Foot.Z;
                NeutralPeak=Foot.Z;
                Next();
                return;
            }
            Aim(1,ThrowInput,FVector(A.X,A.Y,Surface(Terrain,A)),&Terrain);
            return;
        }
        if (NeutralStep==3)
        {
            if (!Shared()) return;
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_SHARED kind=Tide state=active peers=3 digest=%s"),*NeutralExpected);
            Next();
            return;
        }
        if (NeutralStep==4)
        {
            if (Age>.65 && Now-LastAction>.55 && Prop.IsTideBoostReady() && Players[2]->GetHero()->GetCharacterMovement()->IsMovingOnGround())
            {
                Capture(TEXT("After"));
                Peers[2]->ClientNeutralInput(JumpInput,B);
                LastAction=Now;
            }
            const auto& Hero=*Players[2]->GetHero();
            NeutralPeak=FMath::Max(NeutralPeak,Hero.GetActorLocation().Z-Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            if (NeutralPeak-NeutralBase>130.) Next();
            return;
        }
        if (NeutralStep==5)
        {
            const auto& Hero=*Players[2]->GetHero();
            const FVector Foot=Hero.GetActorLocation()-FVector(0,0,Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            NeutralPeak=FMath::Max(NeutralPeak,Foot.Z);
            if (!Hero.GetCharacterMovement()->IsMovingOnGround()) return;
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_TIDE_BOOST team=1 height=%.0f advance=%.0f landed_rise=%.0f"),NeutralPeak-NeutralBase,
                FVector::Dist2D(Foot,B),Foot.Z-NeutralBase);
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_RANGE tide_entry_offset_cm=%.0f tide_landing_from_mouth_cm=%.0f"),
                FVector::Dist2D(NeutralWalkFrom,B),FVector::Dist2D(Foot,B));
            Require(Foot.Z-NeutralBase>18. && FVector::Dist2D(Foot,B)>1000., TEXT("outer spray launches the guest onto walkable ground over ten metres from the mouth"));
            Mode->RecoverPlayer(*Players[2]);
            Next();
            return;
        }
        if (NeutralStep==6)
        {
            if (Prop.GetPlugMask()==3) { Next(); return; }
            Aim(2,ThrowInput,FVector(B.X,B.Y,Surface(Terrain,B)),&Terrain);
            return;
        }
        if (NeutralStep==7)
        {
            if (!Shared()) return;
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_SHARED kind=Tide state=overflow peers=3 digest=%s"),*NeutralExpected);
            Next();
            return;
        }
        if (NeutralStep==8)
        {
            if (!(Prop.GetPlugMask()&1)) { Next(); return; }
            Aim(2,DigInput,FVector(A.X,A.Y,Surface(Terrain,A)),&Terrain);
            return;
        }
        if (NeutralStep==9)
        {
            if (Prop.GetPlugMask()==0)
            {
                UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_TIDE_REOPEN mask=0 overflow=1"));
                Done();
                return;
            }
            Aim(1,DigInput,FVector(B.X,B.Y,Surface(Terrain,B)),&Terrain);
        }
    }
}
