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
    constexpr uint8 JumpInput = AIslandProbePeer::NeutralJumpButton;
    const TCHAR* Names[] = {TEXT("Bridge"), TEXT("Dam"), TEXT("SandDepot"), TEXT("WindTower")};

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
            Entry += FString::Printf(TEXT(":%d/%d"), Prop.GetCompletedRouteSections(), Prop.GetRouteSectionCount());
            for (const FVector& Node : Prop.GetRouteNodes())
            {
                FHitResult Hit;
                World.LineTraceSingleByChannel(Hit, Node + FVector(0,0,700), Node - FVector(0,0,400),
                    ECC_Visibility, FCollisionQueryParams(SCENE_QUERY_STAT(RouteObservation)));
                Entry += FString::Printf(TEXT("/%d,%d,%d:%d"), FMath::RoundToInt(Node.X), FMath::RoundToInt(Node.Y),
                    FMath::RoundToInt(Node.Z), Hit.bBlockingHit ? FMath::RoundToInt(Hit.ImpactPoint.Z) : -10000);
            }
            Entries.Add(Entry);
        }
        Entries.Sort();
        const FString Text = FString::Join(Entries, TEXT("|"));
        const uint32 StateCRC=FCrc::StrCrc32(*Text);
        static uint32 LastLoggedCRC=0;
        if (StateCRC!=LastLoggedCRC)
        {
            LastLoggedCRC=StateCRC;
            UE_LOG(LogTemp,Display,TEXT("ROUTE_QA_GEOMETRY crc=%08x rows=%s"),StateCRC,*Text);
        }
        return FString::Printf(TEXT("%d:%u:%08x:%08x"), Entries.Num(), Terrain->GetAppliedSequence(),
            Terrain->GetChecksum(), StateCRC);
    }

    void Photo(const TCHAR* Kind, const TCHAR* State)
    {
        if (!FParse::Param(FCommandLine::Get(), TEXT("IslandScreenshot"))) return;
        FString Dir = FPaths::ScreenShotDir();
        FParse::Value(FCommandLine::Get(), TEXT("FieldShotsDir="), Dir);
        IFileManager::Get().MakeDirectory(*Dir, true);
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("Neutral%s%s.png"),Kind,State)), false, false);
    }

}

void AIslandProbePeer::ClientNeutralInput_Implementation(uint8 Action, FVector Target)
{
    auto* Player = CastChecked<AIslandPlayerController>(GetOwner());
    const auto* Hero = Player->GetHero();
    if (!Hero) return;
    const FKey Key = Action == JumpInput ? EKeys::SpaceBar : Action == ThrowInput ? EKeys::RightMouseButton : EKeys::LeftMouseButton;
    if (Action != JumpInput)
    {
        if (!Hero->IsInFirstPersonView())
        {
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V, IE_Pressed, 1.f));
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V, IE_Released, 0.f));
        }
        const FVector Aim=Action==ThrowInput ? BankAim(Hero->GetAimOrigin(),Target) : (Target-Hero->GetAimOrigin()).GetSafeNormal();
        Player->SetControlRotation(Aim.Rotation());
    }
    FTimerHandle Press;
    GetWorldTimerManager().SetTimer(Press, FTimerDelegate::CreateWeakLambda(Player,[Player,Key,Target]
    {
        if (const auto* Pawn=Player->GetHero(); Pawn && Key==EKeys::SpaceBar)
            UE_LOG(LogTemp,Display,TEXT("ROUTE_QA_JUMP_BUTTON foot=%s ground=%d can_jump=%d"),
                *(Pawn->GetActorLocation()-FVector(0,0,Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())).ToCompactString(),
                Pawn->GetCharacterMovement()->IsMovingOnGround(),Pawn->CanJump());
        if (const auto* Pawn=Player->GetHero(); Pawn && Key!=EKeys::SpaceBar)
        {
            FVector Eye;
            FRotator View;
            Player->GetPlayerViewPoint(Eye,View);
            FHitResult Contact;
            AIslandNeutralProp::TraceShovel(*Player->GetWorld(),Contact,Eye,Eye+View.Vector()*235.,
                FCollisionQueryParams(SCENE_QUERY_STAT(RouteInput),false,Pawn));
            UE_LOG(LogTemp,Display,TEXT("ROUTE_QA_BUTTON first=%d target=%s eye=%s distance=%.0f hit=%s component=%s"),
                Pawn->IsInFirstPersonView(),*Target.ToCompactString(),*Eye.ToCompactString(),FVector::Dist(Eye,Target),
                *GetNameSafe(Contact.GetActor()),*GetNameSafe(Contact.GetComponent()));
        }
        Player->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
        FTimerHandle Release;
        Player->GetWorldTimerManager().SetTimer(Release, FTimerDelegate::CreateWeakLambda(Player,[Player,Key]
        {
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
        }), .14f, false);
    }), .18f, false);
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
    NeutralProps.SetNum(UE_ARRAY_COUNT(Names));
    for (TActorIterator<AIslandNeutralProp> It(GetWorld()); It; ++It)
    {
        const int32 Kind = int32(It->GetKind()) - int32(EIslandNeutralKind::Bridge);
        if (NeutralProps.IsValidIndex(Kind) && (!NeutralProps[Kind].IsValid() || It->GetActorLocation().X < NeutralProps[Kind]->GetActorLocation().X))
            NeutralProps[Kind] = *It;
    }
    for (const auto& Prop : NeutralProps)
        if (!Prop.IsValid()) { Require(false, TEXT("each public route connects a different pair of search areas")); return; }
    RoomCheck = ERoomCheck::Neutral;
    int32 OnlyKind = 0;
    if (FParse::Value(FCommandLine::Get(),TEXT("NeutralOnlyKind="),OnlyKind))
        NeutralKind = FMath::Clamp(OnlyKind,0,3);
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
    const auto& Nodes = Prop.GetRouteNodes();
    if (Nodes.Num() < 2) return;
    const FVector Along = (Nodes.Last()-Nodes[0]).GetSafeNormal2D();
    const FVector Side = Along.RotateAngleAxis(90.,FVector::UpVector);
    const FVector Subject = FMath::Lerp(Nodes[0],Nodes.Last(),.42);
    FVector View = Subject - Along*2600. + Side*3500. + FVector(0,0,2900.);
    View.Z = FMath::Max(View.Z, Surface(*Mode->GetTerrain(),View)+400.);
    NeutralCamera->SetActorLocationAndRotation(View,(Subject-View).Rotation());
    NeutralCamera->GetCameraComponent()->SetFieldOfView(79.f);
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
            if (NeutralStep==5 && Seat==1)
            {
                const FVector RivalEye=Mode->GetPlayers()[2]->GetHero()->GetAimOrigin();
                if (FVector::Dist2D(EyeAt,RivalEye)<145. ||
                    FMath::PointDistToSegment(RivalEye,EyeAt,Target)<80.) continue;
            }
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

void AIslandProbe::TickNeutralCheck(double Now)
{
    if (FParse::Param(FCommandLine::Get(),TEXT("SceneryReplayQA")))
    {
        TickSceneryReplay(Now);
        return;
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("NeutralDemolitionQA")))
    {
        TickSceneryCheck(Now);
        return;
    }
    const double Age = Now-StepAt;
    auto& Prop = *NeutralProps[NeutralKind].Get();
    if (NeutralStep==6 && !bWindChainDone && Prop.GetKind()==EIslandNeutralKind::WindTower &&
        Prop.GetCompletedRouteSections()==Prop.GetRouteSectionCount())
    {
        int32 OnlyKind;
        if (FParse::Value(FCommandLine::Get(),TEXT("NeutralOnlyKind="),OnlyKind)) SetNeutralStep(30,Now);
    }
    if (NeutralStep>=30 && NeutralStep<=39) { TickWindChain(Prop,Now); return; }
    const auto& Nodes = Prop.GetRouteNodes();
    const auto& Players = Mode->GetPlayers();
    auto& Traveller = *Players[2]->GetHero();
    const FVector Foot = Traveller.GetActorLocation() - FVector(0,0,Traveller.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const bool bWind = Prop.GetKind()==EIslandNeutralKind::WindTower;
    const bool bWater = Prop.GetKind()==EIslandNeutralKind::Dam;
    const bool bSand = Prop.GetKind()==EIslandNeutralKind::SandDepot;
    const uint8 Correct = (bWind || bSand) ? ThrowInput : DigInput;
    auto Next = [&]{ SetNeutralStep(NeutralStep+1,Now); };
    if (Nodes.Num()<2 || Age>95.)
    {
        UE_LOG(LogTemp,Error,TEXT("ROUTE_QA_TIMEOUT kind=%s step=%d progress=%d sections=%d/%d nodes=%d foot=%s goal=%s reports=%d"),
            Names[NeutralKind],NeutralStep,Prop.GetPhase(),Prop.GetCompletedRouteSections(),Prop.GetRouteSectionCount(),
            Nodes.Num(),*Foot.ToCompactString(),*NeutralWalkTo.ToCompactString(),Reports.Num());
        Require(false,TEXT("cross-island route did not become usable")); return;
    }
    if (bNeutralWaitingInput)
    {
        if (Now>=NeutralSendAt)
        {
            Peers[NeutralPendingSeat]->ClientNeutralInput(NeutralPendingAction,NeutralAimPoint);
            bNeutralWaitingInput=false;
            LastAction=Now;
        }
        return;
    }
    if (NeutralStep==0)
    {
        NeutralFrame(Prop);
        if (bSand) { FrameTimes.Reset(); StressStart=Mode->GetTerrain()->GetAppliedSequence(); LastFrame=Now; }
        NeutralBase=Surface(*Mode->GetTerrain(),Nodes[Nodes.Num()-2]);
        Next(); return;
    }
    if (NeutralStep==1 || NeutralStep==90)
    {
        if (NeutralExpected.IsEmpty()) NeutralExpected=Digest(*GetWorld());
        if (NeutralExpected.IsEmpty()) return;
        for (int32 Seat=0;Seat<Peers.Num();++Seat)
            if (!Reports.Contains(Seat) && Now-LastAction>.25) Peers[Seat]->ClientNeutralInspect(NeutralKind*100+NeutralStep);
        if (Now-LastAction>.25) LastAction=Now;
        if (Reports.Num()!=Peers.Num()) return;
        UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_SHARED kind=%s state=%s peers=3 digest=%s"),Names[NeutralKind],
            NeutralStep==1?TEXT("before"):TEXT("after"),*NeutralExpected);
        Photo(Names[NeutralKind],NeutralStep==1 ? TEXT("Before") : TEXT("After"));
        Next(); return;
    }
    if (NeutralStep==91)
    {
        if (Age<.4) return;
        int32 Only=0;
        const bool bSingle=FParse::Value(FCommandLine::Get(),TEXT("NeutralOnlyKind="),Only);
        if (++NeutralKind==UE_ARRAY_COUNT(Names) || bSingle)
        {
            UE_LOG(LogTemp,Display,TEXT("NEUTRAL_QA_DONE players=3 teams=2 kinds=%d"),bSingle ? 1 : int32(UE_ARRAY_COUNT(Names)));
            Advance(EStep::Done); return;
        }
        SetNeutralStep(0,Now); return;
    }
    if (bSand && (NeutralStep==6 || (NeutralStep==5 && Prop.GetPhase()==3)))
    {
        if (LastFrame>0.) FrameTimes.Add((Now-LastFrame)*1000.);
        LastFrame=Now;
    }
    if (NeutralStep>=2 && NeutralStep<=5)
    {
        const bool bWrong=NeutralStep==2;
        if ((bWrong && NeutralStroke && Now-LastAction>1.2) || (!bWrong && Prop.GetPhase()>=NeutralStep-2))
        {
            if (bWrong) Require(Prop.GetPhase()==0,TEXT("opposite tool must not charge a station"));
            if (bSand && NeutralStep==4)
            {
                LastFrame=0.;
                StressStart=Mode->GetTerrain()->GetAppliedSequence();
            }
            if (NeutralStep==5)
            {
                if (Now-LastAction<.65) return;
                Mode->RecoverPlayer(*Players[1]);
                Mode->RecoverPlayer(*Players[2]);
            }
            NeutralStroke=0;
            Next(); return;
        }
        if (Now-LastAction<1.4 || (bWrong && NeutralStroke)) return;
        const uint8 Action=bWrong ? (Correct==DigInput ? ThrowInput : DigInput) : Correct;
        // Select a real, exposed component on a different side for successive hits.
        const auto AimFromSide=[&](int32 Seat,int32 Offset)
        {
            const FVector Origin=Nodes[0];
            for (int32 I=0;I<24;++I)
            {
                const FVector In=Prop.GetActorForwardVector().RotateAngleAxis((I+NeutralStep*3+Offset)*15.,FVector::UpVector);
                for (const double H : {80.,125.,48.,170.})
                {
                    const FVector Center=Origin+FVector(0,0,H);
                    FHitResult Hit;
                    if (AIslandNeutralProp::TraceShovel(*GetWorld(),Hit,Center+In*730.,Center-In*90.,
                            FCollisionQueryParams(SCENE_QUERY_STAT(RouteControlFace),false,Players[Seat]->GetPawn()))
                        && Hit.GetActor()==&Prop && NeutralAim(Seat,Action,Hit.ImpactPoint,&Prop)) return true;
                }
            }
            return false;
        };
        if (NeutralStep==5)
        {
            // Both crews send the final stroke before either sees the resulting state.
            if (!AimFromSide(2,12)) { LastAction=Now; return; }
            const FVector RivalTarget=NeutralAimPoint;
            bNeutralWaitingInput=false;
            if (AimFromSide(1,0))
            {
                auto* Rival=Peers[2].Get();
                FTimerHandle Simultaneous;
                GetWorldTimerManager().SetTimer(Simultaneous,FTimerDelegate::CreateWeakLambda(Rival,[Rival,Action,RivalTarget]
                {
                    Rival->ClientNeutralInput(Action,RivalTarget);
                }),.5f,false);
                ++NeutralStroke;
                UE_LOG(LogTemp,Display,TEXT("ROUTE_QA_CONCURRENT kind=%s prior_hits=%d"),Names[NeutralKind],Prop.GetPhase());
            }
        }
        else if (AimFromSide(1,0)) ++NeutralStroke;
        LastAction=Now;
        return;
    }
    if (NeutralStep==6)
    {
        if (Prop.GetRouteSectionCount()<=0 || Prop.GetCompletedRouteSections()!=Prop.GetRouteSectionCount()) return;
        if (bSand && !FrameTimes.IsEmpty())
        {
            FrameTimes.Sort();
            UE_LOG(LogTemp,Display,TEXT("ROUTE_QA_POUR first=%u last=%u frames=%d frame95_ms=%.3f"),
                StressStart+1,Mode->GetTerrain()->GetAppliedSequence(),FrameTimes.Num(),FrameTimes[FMath::CeilToInt(FrameTimes.Num()*.95)-1]);
        }
        const FVector Far=bWind ? Prop.GetWindJumpStart(2) : Nodes.Last();
        const double Span=FVector::Dist2D(Far,Nodes[0]);
        Require(Span>=5499.,TEXT("the far end lies at least fifty-five metres from activation"));
        if (bWind)
        {
            FVector Floor;
            if (!NeutralFloorPose(2,Far,Floor)) return;
            Place(2,Floor+FVector(0,0,Traveller.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3.));
            NeutralWalkFrom=Floor;
            NeutralWalkTo=Prop.GetWindJumpLanding(2);
        }
        else
        {
            FVector Start,End;
            if (!NeutralFloorPose(2,Nodes[Nodes.Num()-3],Start) || !NeutralFloorPose(2,Far,End)) return;
            NeutralWalk(2,Start,End);
            if (Prop.GetKind()==EIslandNeutralKind::Bridge)
            {
                const int32 Node=Nodes.Num()-3;
                FHitResult DeckHit;
                const bool bOnDeck=GetWorld()->LineTraceSingleByChannel(DeckHit,Nodes[Node]+FVector(0,0,650.),
                    Nodes[Node]-FVector(0,0,40.),ECC_Visibility,FCollisionQueryParams(SCENE_QUERY_STAT(FarBridge),false,&Traveller));
                Require(bOnDeck && DeckHit.GetActor()==&Prop && Start.Z-Nodes[Node].Z>45.,
                    TEXT("far-end guest actually stands on the raised bridge"));
            }
            if (bSand)
                Require(Surface(*Mode->GetTerrain(),Nodes[Nodes.Num()-2])-NeutralBase>25.,TEXT("far earthwork raises editable ground"));
        }
        NeutralPeak=0.;
        Next(); return;
    }
    if (NeutralStep==7)
    {
        if (Age<.8) return;
        if (bWind)
        {
            if (!Traveller.GetCharacterMovement()->IsMovingOnGround() || !Traveller.CanJump() ||
                FVector::Dist2D(Foot,NeutralWalkFrom)>30.) return;
            UE_LOG(LogTemp,Display,TEXT("ROUTE_QA_JUMP_REQUEST foot=%s target=%s"),*Foot.ToCompactString(),*NeutralWalkTo.ToCompactString());
            Peers[2]->ClientNeutralInput(JumpInput,NeutralWalkTo);
        }
        else Peers[2]->ClientWalkToDig(NeutralWalkTo);
        Next(); return;
    }
    if (NeutralStep==8)
    {
        NeutralPeak=FMath::Max(NeutralPeak,bWind ? Foot.Z-NeutralWalkFrom.Z : Traveller.GetVelocity().Size2D());
        const double AdvanceCm=FVector::Dist2D(NeutralWalkFrom,Foot);
        const double Distance=FVector::Dist2D(Nodes[0],Foot);
        if (bWind)
        {
            if (NeutralPeak<125. || AdvanceCm<100. || !Traveller.GetCharacterMovement()->IsMovingOnGround()) return;
        }
        else
        {
            if (bWater && NeutralPeak<=Traveller.GetCharacterMovement()->MaxWalkSpeed+15.) return;
            if (!NeutralWalkArrived(2)) return;
        }
        UE_LOG(LogTemp,Display,TEXT("ROUTE_QA_TRAVEL kind=%s team=%d span_cm=%.0f measured_cm=%.0f advance_cm=%.0f peak=%.1f sections=%d"),
            Names[NeutralKind],CastChecked<AIslandPlayerController>(Players[2].Get())->GetTeam(),FVector::Dist2D(Nodes.Last(),Nodes[0]),Distance,
            AdvanceCm,NeutralPeak,Prop.GetCompletedRouteSections());
        // Walk parallel to the same distant section, outside the footprint of water, wind or deck.
        const FVector Along=(Nodes.Last()-Nodes[Nodes.Num()-2]).GetSafeNormal2D();
        const FVector Side=FVector::CrossProduct(FVector::UpVector,Along);
        for (const double Offset : {490.,-490.,650.,-650.})
        {
            const FVector B=Nodes.Last()+Side*Offset;
            FVector Start,End;
            if (!NeutralFloorPose(2,B-Along*280.,Start) || !NeutralFloorPose(2,B,End) ||
                FMath::Abs(Start.Z-End.Z)>120.) continue;
            NeutralWalk(2,Start,End);
            NeutralPeak=0.;
            Next(); return;
        }
        Require(false,TEXT("far route needs an ordinary walking lane")); return;
    }
    if (NeutralStep==9)
    {
        if (Age<.7) return;
        if (Now-LastAction>1.) { Peers[2]->ClientWalkToDig(NeutralWalkTo); LastAction=Now; }
        NeutralPeak=FMath::Max(NeutralPeak,Traveller.GetVelocity().Size2D());
        if (!NeutralWalkArrived(2)) return;
        Require(NeutralPeak<=Traveller.GetCharacterMovement()->MaxWalkSpeed+20.,TEXT("a player on the bank keeps normal movement"));
        UE_LOG(LogTemp,Display,TEXT("ROUTE_QA_BYPASS kind=%s advance_cm=%.0f peak_speed=%.0f"),Names[NeutralKind],
            FVector::Dist2D(NeutralWalkFrom,Foot),NeutralPeak);
        Mode->RecoverPlayer(*Players[1]); Mode->RecoverPlayer(*Players[2]);
        SetNeutralStep(90,Now);
    }
}
