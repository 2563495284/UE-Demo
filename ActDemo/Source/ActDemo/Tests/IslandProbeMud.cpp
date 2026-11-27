#include "Tests/IslandProbe.h"

#include "Abilities/ExpeditionAbility.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Island/ExpeditionBodyMud.h"
#include "Island/ExpeditionMudBall.h"
#include "Island/IslandAttachedMud.h"
#include "Island/IslandNeutralProp.h"
#include "Voxel/IslandRouteVoxel.h"
#include "TimerManager.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    TOptional<FVector> MudGround(const AVoxelTerrain& Terrain, const FVector& P)
    {
        FHitResult Hit;
        if (!Terrain.TraceRock(FVector(P.X,P.Y,1800.), FVector(P.X,P.Y,-750.), nullptr, Hit)) return {};
        return Hit.ImpactPoint;
    }

    void MudPhoto(const TCHAR* Name)
    {
        if (!FParse::Param(FCommandLine::Get(),TEXT("IslandScreenshot"))) return;
        FString Dir=FPaths::ScreenShotDir();
        FParse::Value(FCommandLine::Get(),TEXT("FieldShotsDir="),Dir);
        IFileManager::Get().MakeDirectory(*Dir,true);
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir,FString(Name)+TEXT(".png")),true,false);
    }

    FVector Feet(const AVoxelCharacter& Hero)
    {
        return Hero.GetActorLocation() - FVector::UpVector * Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    }
}

void AIslandProbePeer::ClientMudStroke_Implementation(bool bThrow, FVector Aim)
{
    auto* Player = CastChecked<AIslandPlayerController>(GetOwner());
    if (bThrow) Player->SubmitExpeditionThrow(Aim.GetSafeNormal());
    else Player->SubmitEdit(EVoxelAction::Punch, Aim.GetSafeNormal());
}

void AIslandProbePeer::ClientMudHold_Implementation(bool bThrow, bool bHeld, FRotator Look)
{
    auto* Player = CastChecked<AIslandPlayerController>(GetOwner());
    Player->SetControlRotation(Look);
    Player->InputKey(FInputKeyEventArgs::CreateSimulated(bThrow ? EKeys::RightMouseButton : EKeys::LeftMouseButton,
        bHeld ? IE_Pressed : IE_Released, bHeld ? 1.f : 0.f));
    UE_LOG(LogTemp,Display,TEXT("MUD_QA_BUTTON right=%d pressed=%d"),bThrow,bHeld);
}

void AIslandProbePeer::ClientMudInspect_Implementation(int32 Stage, FVector Origin)
{
    const auto* Terrain = AVoxelTerrain::Find(GetWorld());
    if (!Terrain || !Terrain->IsReady()) return;
    const auto Floor = MudGround(*Terrain, Origin);
    if (!Floor) return;
    const int32 Top = FMath::RoundToInt(Floor->Z - Origin.Z);
    UE_LOG(LogTemp,Display,TEXT("MUD_QA_LOCAL stage=%d seq=%u crc=%08x top=%d"),Stage,Terrain->GetAppliedSequence(),Terrain->GetChecksum(),Top);
    ServerMudReport(Stage,Terrain->GetAppliedSequence(),Terrain->GetChecksum(),Top);
}

void AIslandProbePeer::ServerMudReport_Implementation(int32 Stage, uint32 Sequence, uint32 Checksum, int32 Top)
{
    if (auto* Probe = Coordinator.Get()) Probe->RecordMud(Seat,Stage,Sequence,Checksum,Top);
}

void AIslandProbe::RecordMud(int32 Seat,int32 Stage,uint32 Sequence,uint32 Checksum,int32 Top)
{
    if (RoomCheck!=ERoomCheck::Mud || Stage!=MudStep) return;
    const auto& Terrain = *Mode->GetTerrain();
    const FVector Origin = Stage == 3 ? MudFoot : MudShaft;
    const auto Floor = MudGround(Terrain,Origin);
    if (Floor && Sequence==Terrain.GetAppliedSequence() && Checksum==Terrain.GetChecksum()
        && FMath::Abs(FMath::RoundToInt(Floor->Z-Origin.Z)-Top)<=2) Reports.Add(Seat);
}

void AIslandProbe::SetMudStep(int32 Next,double Now)
{
    MudStep=Next;
    StepAt=Now;
    LastAction=-100.;
    bIssued=false;
    MudShots=0;
    Reports.Reset();
    UE_LOG(LogTemp,Display,TEXT("MUD_QA_STEP stage=%d"),Next);
}


AIslandMudProbeSurface::AIslandMudProbeSurface()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    Field = CreateDefaultSubobject<UIslandRouteVoxelComponent>(TEXT("WallAndDeck"));
    SetRootComponent(Field);
}

void AIslandMudProbeSurface::BeginPlay()
{
    Super::BeginPlay();
    // The elevated ledge lets a standing thrower hit an ankle nearly horizontally.
    Field->InitializeField(FBox(FVector(-340,-580,-120),FVector(470,365,320)),10.,[](FVector P)
    {
        const auto Box = [P](FVector Center, FVector Extent)
        {
            return ((P - Center).GetAbs() - Extent).GetMax();
        };
        const double Wall = Box({-15,0,140},{15,195,160});
        // Expose the sides of both boots. A wide tabletop would intercept every low side shot.
        const double Deck = Box({0,-400,100},{26,95,30});
        const double Landing = Box({65,-105,-52},{385,455,52});
        return FRouteVoxelSample{FMath::Min3(Wall,Deck,Landing), FColor(142,126,104)};
    });
}

namespace
{
    uint32 MudDigest(const AVoxelCharacter& Hero)
    {
        const auto* Mud=Hero.FindComponentByClass<UExpeditionBodyMudComponent>();
        uint32 Hash=0;
        for (int32 I=0; I<Mud->Num(); ++I)
        {
            FVector Center;
            float R=0.f;
            if (!Mud->GetClodWorld(I,Center,R)) continue;
            const FVector Local=Hero.GetActorTransform().InverseTransformPosition(Center);
            const FIntVector Q(FMath::RoundToInt(Local.X*10.), FMath::RoundToInt(Local.Y*10.), FMath::RoundToInt(Local.Z*10.));
            Hash=HashCombineFast(Hash,GetTypeHash(Q));
        }
        return Hash;
    }

    TOptional<FVector> AnkleShot(const AVoxelCharacter& Shooter, const AVoxelCharacter& Victim)
    {
        const FVector Origin=Shooter.GetAimOrigin();
        const FVector Foot=Feet(Victim);
        const FVector Near=(Origin-Foot).GetSafeNormal2D();
        const FVector Side=FVector::CrossProduct(FVector::UpVector,Near);
        // Select an aimed point on the exposed lower hemisphere. Sample the actual flight,
        // including scenery in front of both the projectile and its intended target.
        for (double Height : {8.,10.,12.,14.,16.,18.,20.,5.,1.,-4.,-9.})
        for (double Offset : {26.,34.,18.,10.,0.,-15.})
        for (double Lateral : {0.,12.,-12.,22.,-22.})
        {
            const FVector Aim=AExpeditionMudBall::AimAtPoint(Origin,Foot+Near*Offset+Side*Lateral+FVector::UpVector*Height);
            const FVector V=AExpeditionMudBall::LaunchVelocity(Aim);
            FVector From=Origin;
            for (int32 N=1; N<=70; ++N)
            {
                const double T=N/60.;
                const FVector To=Origin+V*T-FVector::UpVector*(.5*AExpeditionMudBall::FallAcceleration*T*T);
                FHitResult Hit;
                if (AExpeditionMudBall::TraceContact(*Shooter.GetWorld(),Shooter,From,To,AExpeditionMudBall::Radius,Hit))
                {
                    if (Hit.GetActor()==&Victim && Hit.Item==INDEX_NONE && Hit.ImpactPoint.Z-Foot.Z<25.)
                    {
                        TArray<FOverlapResult> Surfaces;
                        const FVector Center=Hit.ImpactPoint+Hit.ImpactNormal*2.;
                        Shooter.GetWorld()->OverlapMultiByChannel(Surfaces,Center,FQuat::Identity,ECC_Visibility,
                            FCollisionShape::MakeSphere(UExpeditionBodyMudComponent::ClodRadius),
                            FCollisionQueryParams(SCENE_QUERY_STAT(MudAnkleTarget),false,&Victim));
                        if (Surfaces.ContainsByPredicate([](const FOverlapResult& Part)
                            { return AIslandAttachedMud::CanReceive(Part.GetComponent()); })) return Aim;
                    }
                    break;
                }
                From=To;
            }
        }
        return {};
    }
}

void AIslandProbePeer::ClientBodyMudInspect_Implementation(int32 Stage, AVoxelCharacter* Subject, FName Photo)
{
    if (!Subject) return;
    auto* Mud=Subject->FindComponentByClass<UExpeditionBodyMudComponent>();
    if (!Mud) return;
    const uint32 Digest=MudDigest(*Subject);
    UE_LOG(LogTemp,Display,TEXT("MUD_QA_BODY_LOCAL stage=%d clods=%d anchored=%d digest=%08x"),
        Stage,Mud->Num(),Mud->IsAnchored(),Digest);
    ServerBodyMudReport(Stage,Digest,Mud->Num(),Mud->IsAnchored());
    if (!Photo.IsNone() && FParse::Param(FCommandLine::Get(),TEXT("IslandScreenshot")))
    {
        auto* PC=CastChecked<AIslandPlayerController>(GetOwner());
        if (PC->GetHero()==Subject && Photo==TEXT("MudFoot") && !Subject->IsInFirstPersonView())
        {
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V,IE_Pressed,1.f));
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::V,IE_Released,0.f));
        }
        if (PC->GetHero()!=Subject)
            PC->SetControlRotation(PC->GetHero()->GetViewRotationForTarget(Subject->GetActorLocation(),PC->GetHero()->GetActorLocation()));
        FTimerHandle Shot;
        GetWorldTimerManager().SetTimer(Shot,[Photo](){MudPhoto(*Photo.ToString());},.1f,false);
    }
}

void AIslandProbePeer::ServerBodyMudReport_Implementation(int32 Stage,uint32 Digest,int32 Clods,bool bAnchored)
{
    BodyMudStage=Stage;
    BodyMudDigest=Digest;
    BodyMudClods=Clods;
    bBodyMudAnchored=bAnchored;
}

bool AIslandProbe::SharedBodyMud(int32 Stage,AVoxelCharacter& Victim,FName Photo,double Now)
{
    const auto* Mud=Victim.FindComponentByClass<UExpeditionBodyMudComponent>();
    const uint32 Digest=MudDigest(Victim);
    bool All=true;
    for (auto& Peer : Peers)
    {
        if (Peer->BodyMudStage==Stage && Peer->BodyMudDigest==Digest && Peer->BodyMudClods==Mud->Num()
            && Peer->bBodyMudAnchored==Mud->IsAnchored()) continue;
        All=false;
        if (Now-LastAction>.3) Peer->ClientBodyMudInspect(Stage,&Victim,Photo);
    }
    if (!All && Now-LastAction>.3) LastAction=Now;
    if (All)
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_AGREED stage=%d peers=%d clods=%d anchored=%d digest=%08x"),
            Stage,Peers.Num(),Mud->Num(),Mud->IsAnchored(),Digest);
    return All;
}

void AIslandProbe::TickMudCheck(double Now)
{
    auto& Terrain=*Mode->GetTerrain();
    auto* Attacker=CastChecked<AIslandPlayerController>(Mode->GetPlayers()[1]); // Also the pit climber.
    auto& Victim=*Attacker->GetHero();
    auto& Shooter=*Mode->GetPlayers()[2]->GetHero();
    auto& Mud=*Victim.FindComponentByClass<UExpeditionBodyMudComponent>();
    const double Age=Now-StepAt;
    auto FireMud=[&](FVector Aim)
    {
        Peers[2]->ClientMudStroke(true,Aim);
        LastAction=Now;
        ++MudShots;
    };
    auto Shared=[&](const FVector& Origin)
    {
        if (Reports.Num()==Peers.Num()) return true;
        if (Now-LastAction>.20)
        {
            for(int32 I=0;I<Peers.Num();++I) if (!Reports.Contains(I)) Peers[I]->ClientMudInspect(MudStep,Origin);
            LastAction=Now;
        }
        return false;
    };
    auto OnLanding=[&](int32 Seat,FVector Near)
    {
        const auto* Hero = Mode->GetPlayers()[Seat]->GetHero();
        Place(Seat, FVector(Near.X,Near.Y,MudFoot.Z+Hero->GetSimpleCollisionHalfHeight()+4.));
    };
    auto WallMud=[&](double Y)
    {
        FHitResult Hit;
        const FVector Point=MudFoot+FVector(0,Y,95);
        return GetWorld()->LineTraceSingleByChannel(Hit,Point+FVector(220,0,0),Point-FVector(40,0,0),ECC_Visibility)
            && Cast<AIslandAttachedMud>(Hit.GetActor());
    };
    if (Age>20.)
    {
        UE_LOG(LogTemp,Error,TEXT("ISLAND_QA_TIMEOUT mud=%d shots=%d clods=%d anchored=%d feet=%s muzzle=%s"),
            MudStep,MudShots,Mud.Num(),Mud.IsAnchored(),*Feet(Victim).ToCompactString(),*Shooter.GetAimOrigin().ToCompactString());
        Require(false,TEXT("mud interaction completed within the scenario deadline"));
        return;
    }
    if (MudStep==0)
    {
        for (double X=-3200.;X<=3200.;X+=400.)
        for (double Y=-3200.;Y<=3200.;Y+=400.)
        {
            const auto Floor=MudGround(Terrain,{X,Y,0.});
            if (!Floor) continue;
            const auto Pit=MudGround(Terrain,*Floor+FVector(0,650,0));
            const auto Lip=MudGround(Terrain,*Floor+FVector(0,855,0));
            if (!Pit || !Lip || FMath::Abs(Pit->Z-Lip->Z)>9.) continue;
            // The wall approach and raised ankle lane share a repeatable walkable voxel
            // surface. The adjacent shaft still uses the real excavatable island terrain.
            double Top=Floor->Z;
            for (int32 DX=-300;DX<=450;DX+=150)
                for (int32 DY=-550;DY<=350;DY+=150)
                {
                    const FVector XY(Floor->X+DX,Floor->Y+DY,0);
                    FHitResult Surface;
                    if (GetWorld()->LineTraceSingleByChannel(Surface,XY+FVector(0,0,2400),XY-FVector(0,0,750),ECC_Visibility))
                        Top=FMath::Max(Top,Surface.ImpactPoint.Z);
                }
            MudFoot=FVector(Floor->X,Floor->Y, Top + 85.);
            MudShaft=*Pit;
            MudWall=GetWorld()->SpawnActor<AIslandMudProbeSurface>(MudFoot,FRotator::ZeroRotator);
            Require(MudWall.IsValid(),TEXT("the wall, walkable approach and raised deck use island voxel collision"));
            OnLanding(0,MudFoot+FVector(330,260,0));
            OnLanding(1,MudFoot+FVector(365,0,0));
            Victim.SetActorRotation(FRotator(0,180,0));
            OnLanding(2,MudFoot+FVector(110,0,0));
            SetMudStep(1,Now);
            return;
        }
        Require(false,TEXT("found a diggable natural shaft with an accessible rim"));
    }
    else if (MudStep==1)
    {
        if (Age<1.) return;
        if (Mud.Num()==3) {SetMudStep(2,Now);return;}
        // Input may reach the host while the previous throw is still cooling down.
        // Advance on three deposited clods, and retry an unaccepted input after flight time.
        if (Mud.Num()<3 && Now-LastAction>1.55)
            FireMud(AExpeditionMudBall::AimAtPoint(Shooter.GetAimOrigin(),Victim.GetActorLocation()+FVector(0,0,15)));
    }
    else if (MudStep==2)
    {
        if (Age<2.) return;
        Require(Mud.Num()==3 && !Mud.IsAnchored(),TEXT("three real torso hits remain attached and leave the runner free"));
        if (!SharedBodyMud(MudStep,Victim,TEXT("MudChest"),Now) || Age<2.6) return;
        OnLanding(2,MudFoot+FVector(210,-270,0));
        Peers[1]->ClientWalkToDig(MudFoot+FVector(45,0,0));
        SetMudStep(3,Now);
    }
    else if (MudStep==3)
    {
        if (!Mud.IsAnchored()) return;
        Require(Mud.Num()==3 && (Feet(Victim)-MudFoot).X<180.,TEXT("the coated runner traversed open soil before reaching the wall"));
        Peers[1]->ClientWalkToDig(Feet(Victim));
        SetMudStep(4,Now);
    }
    else if (MudStep==4)
    {
        if (!SharedBodyMud(MudStep,Victim,TEXT("MudWall"),Now) || Age<.7) return;
        // Reset this fixture through the normal recovery path, then shoot towards the opponent behind the wall.
        OnLanding(1,MudFoot+FVector(-85,-125,0));
        OnLanding(2,MudFoot+FVector(210,-125,0));
        SetMudStep(20,Now);
    }
    else if (MudStep==20 || MudStep==21)
    {
        const double Lane=MudStep==20 ? -125. : 100.;
        if (Age<.8) return;
        if (!bIssued)
        {
            const FVector Point=MudFoot+FVector(0,Lane,95);
            FireMud(AExpeditionMudBall::AimAtPoint(Shooter.GetAimOrigin(),Point));
            bIssued=true;
            return;
        }
        if (!WallMud(Lane)) return;
        Require(!Mud.Num() && !Mud.IsAnchored(),TEXT("actual clay remained on the near side of the wall and did not coat the bystander"));
        if (MudStep==20)
        {
            UE_LOG(LogTemp,Display,TEXT("MUD_QA_WALL_OCCLUSION behind_clods=%d wall_pad=1"),Mud.Num());
            OnLanding(1,MudFoot+FVector(43,32,0));
            OnLanding(2,MudFoot+FVector(230,130,0));
            SetMudStep(21,Now);
        }
        else
        {
            UE_LOG(LogTemp,Display,TEXT("MUD_QA_CLIPPED wall_pad=1 bystander_clods=%d"),Mud.Num());
            const FVector Deck=MudFoot+FVector(0,-400,130);
            Place(1,Deck+FVector(0,0,Victim.GetSimpleCollisionHalfHeight()+4.));
            OnLanding(2,Deck+FVector(-215,0,0));
            OnLanding(0,Deck+FVector(0,-220,0));
            SetMudStep(22,Now);
        }
    }
    else if (MudStep==22 || MudStep==24)
    {
        if (Age<.9) return;
        if (!bIssued)
        {
            const auto Aim=AnkleShot(Shooter,Victim);
            if (!Aim) {Require(false,TEXT("a clear projectile ray reaches the supporting ankle on the raised deck"));return;}
            FireMud(*Aim);
            bIssued=true;
        }
        const int32 Expected=MudStep==22 ? 1 : 2;
        if (Mud.Num()!=Expected || !Mud.IsAnchored()) return;
        SetMudStep(MudStep+1,Now);
    }
    else if (MudStep==23 || MudStep==25)
    {
        if (!SharedBodyMud(MudStep,Victim,MudStep==23 ? TEXT("MudFoot") : TEXT("MudDouble"),Now) || Age<.7) return;
        if (MudStep==23)
        {
            OnLanding(2,MudFoot+FVector(215,-400,0));
            SetMudStep(24,Now);
        }
        else
        {
            FVector A,B; float R=0;
            Require(Mud.GetClodWorld(0,A,R) && Mud.GetClodWorld(1,B,R) && FVector::Dist(A,B)>2.*R,
                TEXT("two distinct ankle deposits connect opposite sides to the deck independently"));
            Peers[1]->ClientMudHold(false,true,FRotator(40,0,0));
            SetMudStep(26,Now);
        }
    }
    else if (MudStep==26)
    {
        if (Age<.35) return;
        if (!bIssued) {Peers[1]->ClientMudHold(false,false,FRotator(40,0,0));bIssued=true;return;}
        if (Mud.Num()!=1 || !Mud.IsAnchored()) return;
        if (!SharedBodyMud(MudStep,Victim,NAME_None,Now)) return;
        FVector Center;float R=0;
        Mud.GetClodWorld(0,Center,R);
        const FVector Out=(Center-Victim.GetActorLocation()).GetSafeNormal2D();
        OnLanding(0,Center+Out*160.);
        SetMudStep(27,Now);
    }
    else if (MudStep==27)
    {
        if (Age<.8) return;
        if (!bIssued)
        {
            FVector Center; float R=0;
            if (!Mud.GetClodWorld(0,Center,R)) {Require(false,TEXT("one intact connection remains for the teammate"));return;}
            const auto* Friend=Mode->GetPlayers()[0]->GetHero();
            Peers[0]->ClientMudStroke(false,(Center-Friend->GetAimOrigin()).GetSafeNormal());
            bIssued=true;
            return;
        }
        if (Mud.Num() || Mud.IsAnchored() || !SharedBodyMud(MudStep,Victim,TEXT("MudFreed"),Now)) return;
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_RESCUE self_removed=1 teammate_removed=1 supports=0"));
        // The duelling deck sits above the scenery. Choose the independent pit from
        // real, exposed island soil with standing clearance along the exit route.
        auto FindPit = [&](FVector Near)
        {
            if (FVector::Dist2D(Near,MudFoot)<720.) return false;
            const auto Pit=MudGround(Terrain,Near);
            if (!Pit) return false;
            const FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(MudPitGround),false);
            FCollisionQueryParams ClearanceQuery(SCENE_QUERY_STAT(MudPitClearance),false);
            ClearanceQuery.AddIgnoredActor(&Terrain);
            for (int32 Offset : {0,90,205})
            {
                const auto Ground=MudGround(Terrain,*Pit+FVector(0,Offset,0));
                if (!Ground || FMath::Abs(Ground->Z-Pit->Z)>12.) return false;
                FHitResult Surface;
                if (!AIslandNeutralProp::TraceShovel(*GetWorld(),Surface,*Ground+FVector(0,0,240),
                    *Ground-FVector(0,0,12),FloorQuery) || Surface.GetActor()!=&Terrain) return false;
                TArray<FHitResult> Parts;
                GetWorld()->SweepMultiByChannel(Parts,*Ground+FVector(0,0,210),*Ground-FVector(0,0,180),
                    FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(48.f),ClearanceQuery);
                if (Parts.ContainsByPredicate([](const FHitResult& Part)
                    { return Part.bBlockingHit || AIslandAttachedMud::CanReceive(Part.GetComponent()) || Cast<AIslandNeutralProp>(Part.GetActor()); })) return false;
            }
            MudShaft=*Pit;
            return true;
        };
        bool Found=FindPit({-1850,-850,0});
        for (int32 X=-2800; !Found && X<=2800; X+=350)
            for (int32 Y=-2800; !Found && Y<=2800; Y+=350) Found=FindPit({double(X),double(Y),0});
        if (!Found) { Require(false,TEXT("the pit has exposed soil and clear walking space up to its rim")); return; }
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_SHAFT_SITE surface=%s"),*MudShaft.ToCompactString());
        SetMudStep(6,Now);
    }
    else if (MudStep==6)
    {
        const auto Floor=MudGround(Terrain,MudShaft);
        if (!Floor || Terrain.IsBusy() || Age<.65) return;
        if (MudShaft.Z-Floor->Z<145.)
        {
            // Arrange the shaft through the replicated terrain edit queue. Throwing,
            // lifting, collision replay and walking out below are all real guest input.
            if (Now-LastAction > .7)
            {
                Mode->EnqueueEdit(*Mode->GetPlayers()[0], EVoxelAction::Punch,FVector::DownVector,
                    VoxelPlayground::QuantizeCenter(*Floor-FVector(0,0,45)));
                LastAction = Now;
            }
            return;
        }
        const auto Lip=MudGround(Terrain,MudShaft+FVector(0.,205.,0.));
        if (!Lip) { Require(false,TEXT("a walkable shaft rim exists above the climber")); return; }
        MudExitLip=*Lip;
        MudShaftSequence=Terrain.GetAppliedSequence();
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_SHAFT dig_depth_cm=%.1f"),MudShaft.Z-Floor->Z);
        MudBase=Floor->Z;
        // Keep the spectator out of the shaft while placing the climber. Two solid
        // capsules on the same spawn point would intercept the underfoot throw.
        Place(0,*Lip+FVector(0,170,200));
        Place(1,*Floor+FVector(0,0,96));
        MudShaft.Z=MudBase;
        Attacker->ClientSetRotation(FRotator(-82.,0.,0.));
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_PIT prepared=1 bottom_z=%.1f"),MudBase);
        const auto* Observer=Mode->GetPlayers()[0]->GetHero();
        Mode->GetPlayers()[0]->ClientResetView(Observer->GetViewRotationForTarget(*Floor,Observer->GetActorLocation()));
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_PIT_CREW observer=%s climber=%s thrower=%s"),
            *Feet(*Observer).ToCompactString(),*Feet(Victim).ToCompactString(),*Feet(Shooter).ToCompactString());
        SetMudStep(7,Now);
    }
    else if (MudStep==7)
    {
        const auto Floor=MudGround(Terrain,MudShaft);
        if (!Floor || Age<.7) return;
        const auto& Climber=*Attacker->GetHero();
        const double FootZ=Feet(Climber).Z;
        // The pad slopes away from the centre. Keep half a walking step for its edge.
        const double StepBudget=Climber.GetCharacterMovement()->MaxStepHeight*.5;
        if (bIssued && Climber.GetCharacterMovement()->IsMovingOnGround() && FMath::Abs(FootZ-Floor->Z)<12.
            && MudExitLip.Z-FMath::Min(FootZ, Floor->Z)<=StepBudget)
        {
            Peers[1]->ClientMudHold(true,false,FRotator(-89.,0.,0.));
            SetMudStep(8,Now);
            return;
        }
        if (!bIssued) { Peers[1]->ClientMudHold(true,true,FRotator(-89.,0.,0.)); bIssued=true; }
        if (Now-LastAction>.3)
        {
            Attacker->ClientSetRotation(FRotator(-82.,0.,0.));
            LastAction=Now;
        }
    }
    else if (MudStep==8)
    {
        const auto Floor=MudGround(Terrain,MudShaft);
        if (!Floor || !Shared(MudShaft)) return;
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_STEPS placed_pads=%u rise_cm=%.1f rim_above_pad_cm=%.1f peers=%d crc=%08x"),
            Terrain.GetAppliedSequence()-MudShaftSequence,Floor->Z-MudBase,MudExitLip.Z-Floor->Z,Reports.Num(),Terrain.GetChecksum());
        MudPhoto(TEXT("MudSteps"));
        SetMudStep(9,Now);
    }
    else if (MudStep==9)
    {
        const FVector Actual=Feet(*Attacker->GetHero());
        const auto Floor=MudGround(Terrain,Actual);
        if (Floor && Attacker->GetHero()->GetCharacterMovement()->IsMovingOnGround()
            && FVector::Dist2D(Actual,MudExitLip)<32. && FMath::Abs(Actual.Z-Floor->Z)<14.
            && FMath::Abs(Actual.Z-MudExitLip.Z)<14.)
        {
            UE_LOG(LogTemp,Display,TEXT("MUD_QA_EXIT walked_cm=%.1f rise_cm=%.1f"),FVector::Dist2D(Actual,MudShaft),Actual.Z-MudBase);
            SetMudStep(16,Now);
            return;
        }
        // The natural lip is uneven. Try adjacent ways out with ordinary walking only.
        if (Now-LastAction<2.1) return;
        const FVector Dir=FRotator(0.,90.+45.*MudShots,0.).Vector();
        const auto Lip=MudGround(Terrain,MudShaft+Dir*205.);
        if (Lip) { MudExitLip=*Lip; Peers[1]->ClientWalkToDig(MudExitLip); }
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_LIP attempt=%d feet=%s gain=%.1f"),MudShots,*Actual.ToCompactString(),Actual.Z-MudBase);
        ++MudShots; LastAction=Now;
    }
    else if (MudStep==16)
    {
        Peers[1]->ClientMudHold(true,true,FRotator(-89.,0.,0.));
        Peers[1]->ClientMudHold(false,true,FRotator(-89.,0.,0.));
        SetMudStep(17,Now);
    }
    else if (MudStep==17 || MudStep==18 || MudStep==19)
    {
        if (Age<1.65) return;
        auto& ASC=*Attacker->GetHero()->GetAbilitySystemComponent();
        const FGameplayAbilitySpec* Dig=ASC.FindAbilitySpecFromClass(UExpeditionDigAbility::StaticClass());
        const FGameplayAbilitySpec* Throw=ASC.FindAbilitySpecFromClass(UExpeditionThrowAbility::StaticClass());
        if (!Dig || !Throw) { Require(false,TEXT("player owns both tool abilities")); return; }
        if (MudStep==17)
        {
            Require(Dig->IsActive() && Throw->IsActive(),TEXT("one guest can hold both mouse buttons at once"));
            Peers[1]->ClientMudHold(false,false,FRotator(-89.,0.,0.));
            SetMudStep(18,Now);
            return;
        }
        if (MudStep==18)
        {
            Require(!Dig->IsActive() && Throw->IsActive(),TEXT("releasing dig keeps the independent throw active"));
            Peers[1]->ClientMudHold(true,false,FRotator(-89.,0.,0.));
            SetMudStep(19,Now);
            return;
        }
        Require(!Dig->IsActive() && !Throw->IsActive(),TEXT("both tools stop when their buttons release"));
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_BUTTONS both_held=1 dig_release_kept_throw=1 both_stopped=1"));
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_DONE"));
        Advance(EStep::Done);
        SetActorTickEnabled(false);
    }
}
