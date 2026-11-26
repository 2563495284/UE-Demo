#include "Tests/IslandProbe.h"

#include "Abilities/ExpeditionAbility.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
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

void AIslandProbe::TickMudCheck(double Now)
{
    auto& Terrain=*Mode->GetTerrain();
    FString ExitRoute;
    FParse::Value(FCommandLine::Get(),TEXT("MudExit="),ExitRoute);
    const bool Rescue=ExitRoute==TEXT("rescue");
    const int32 ShooterSeat=Rescue ? 2 : 1;
    const int32 VictimSeat=Rescue ? 1 : 2;
    auto* Attacker=CastChecked<AIslandPlayerController>(Mode->GetPlayers()[ShooterSeat]);
    auto* Target=CastChecked<AIslandPlayerController>(Mode->GetPlayers()[VictimSeat]);
    auto& Victim=*Target->GetHero();
    const double Age=Now-StepAt;
    auto AimAt=[&](int32 Seat,bool bThrow,const FVector& Point)
    {
        Peers[Seat]->ClientMudStroke(bThrow,(Point-Mode->GetPlayers()[Seat]->GetHero()->GetAimOrigin()).GetSafeNormal());
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
    if (Age>20.)
    {
        UE_LOG(LogTemp,Error,TEXT("ISLAND_QA_TIMEOUT mud=%d shots=%d seq=%u victim=%s stunned=%d compact=%d"),
            MudStep,MudShots,Terrain.GetAppliedSequence(),*Feet(Victim).ToCompactString(),Victim.IsMudStunned(),Victim.IsMudCompact());
        Require(false,TEXT("mud interaction completed within the scenario deadline"));
        return;
    }
    if (MudStep==0)
    {
        // Find an actual flat patch, clear of previously generated landmarks and obstacles.
        for (double X=-3200.; X<=3200.; X+=450.)
            for (double Y=-3200.; Y<=3200.; Y+=450.)
            {
                const auto Floor=MudGround(Terrain,{X,Y,0.});
                if (!Floor) continue;
                const auto Shooter=MudGround(Terrain,*Floor-FVector(280,0,0));
                const auto Pit=MudGround(Terrain,*Floor+FVector(0,550,0));
                const auto Lip=MudGround(Terrain,*Floor+FVector(0,700,0));
                if (!Shooter || !Pit || !Lip || FMath::Abs(Shooter->Z-Floor->Z)>8. || FMath::Abs(Pit->Z-Lip->Z)>8.) continue;
                FHitResult Obstacle;
                if (GetWorld()->LineTraceSingleByChannel(Obstacle,*Shooter+FVector(0,0,90),*Floor+FVector(0,0,90),ECC_Visibility)) continue;
                MudFoot=*Floor;
                MudShaft=*Pit;
                Place(ShooterSeat,*Shooter+FVector(0,0,96));
                Place(VictimSeat,*Floor+FVector(0,0,96));
                Place(0,*Floor+FVector(-180,-330,105));
                Mode->GetPlayers()[0]->ClientResetView(FRotator(-12,63,0));
                SetMudStep(1,Now);
                return;
            }
        Require(false,TEXT("found a flat expedition mud duel site"));
    }
    else if (MudStep==1)
    {
        if (Age<.65) return;
        if (!bIssued) { MudPhoto(TEXT("MudDuel")); bIssued=true; return; }
        if (Age<1.5) return;
        if (Victim.IsMudStunned())
        {
            Require(MudShots==3,TEXT("exactly three rival throws knock down the guest"));
            MudFoot=Mode->BurialSites.FindChecked(Target).Foot;
            MudKnockTime=GetWorld()->GetTimeSeconds();
            SetMudStep(2,Now);
            LastAction=Now-.13;
            return;
        }
        if (MudShots<3 && Now-LastAction>.78) AimAt(ShooterSeat,true,Feet(Victim)+FVector(0,0,96));
    }
    else if (MudStep==2)
    {
        const auto& Site=Mode->BurialSites.FindChecked(Target);
        if (Site.Layers==3)
        {
            Require(Victim.IsMudStunned(),TEXT("single thrower closes the shell before the victim wakes"));
            MudPhoto(TEXT("MudShell"));
            SetMudStep(3,Now);
            return;
        }
        if (Site.Layers>0 && MudShots<2 && Now-LastAction>.66)
            AimAt(ShooterSeat,true,MudFoot+FVector(0,0,36));
    }
    else if (MudStep==3)
    {
        const auto Top=MudGround(Terrain,MudFoot);
        if (!Top || !Shared(MudFoot) || Victim.IsMudStunned()) return;
        const double Height=Top->Z-MudFoot.Z;
        const FVector Eye=Victim.GetAimOrigin();
        FHitResult Wall;
        Require(Height>=104. && Height<=134. && Victim.IsMudCompact() && Victim.IsMudImmune()
            && Terrain.TraceRock(Eye,Eye+FVector(0,175,0),&Victim,Wall)
            && GetWorld()->LineTraceTestByChannel(Attacker->GetHero()->GetAimOrigin(),Eye,ECC_Visibility,
                FCollisionQueryParams(SCENE_QUERY_STAT(MudCoverSight),false,Attacker->GetHero())),TEXT("actual closed collision, low posture and wake immunity"));
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_COVER throws=5 peers=%d height_cm=%.1f stun_seconds=%.3f crc=%08x"),
            Reports.Num(),Height,GetWorld()->GetTimeSeconds()-MudKnockTime,Terrain.GetChecksum());
        if (!ExitRoute.IsEmpty())
        {
            if (Rescue) Place(0,*Top+FVector(0,0,96));
            SetMudStep(40,Now);
        }
        else SetMudStep(4,Now);
    }
    else if (MudStep==40)
    {
        if (Age<.45) return;
        const bool Below=ExitRoute==TEXT("under");
        const FVector Eye=Victim.GetAimOrigin();
        FHitResult Ceiling;
        const bool Closed=Terrain.TraceRock(Eye,Eye+FVector::UpVector*300.,&Victim,Ceiling);
        const auto Top=MudGround(Terrain,MudFoot);
        const double Depth=MudFoot.Z-Feet(Victim).Z;
        if (Below ? (Depth>30. && Closed && Top && Top->Z-MudFoot.Z>104.) : (!Closed && !Victim.IsMudCompact()))
        {
            UE_LOG(LogTemp,Display,TEXT("MUD_QA_ESCAPE_VARIANT route=%s strokes=%d floor_drop_cm=%.1f roof_closed=%d standing=%d"),
                *ExitRoute,MudShots,Depth,Closed,!Victim.IsMudCompact());
            UE_LOG(LogTemp,Display,TEXT("MUD_QA_DONE"));
            Advance(EStep::Done);
            SetActorTickEnabled(false);
            return;
        }
        if (Now-LastAction>.68 && MudShots<9)
        {
            const int32 Digger=Rescue ? 0 : VictimSeat;
            if (Rescue) Require(Mode->GetPlayers()[0]->GetPlayerState<AIslandPlayerState>()->Team
                == Target->GetPlayerState<AIslandPlayerState>()->Team,TEXT("the rescuer belongs to the buried player's crew"));
            Peers[Digger]->ClientMudStroke(false,Rescue || Below ? FVector::DownVector : FVector::UpVector);
            LastAction=Now; ++MudShots;
        }
    }
    else if (MudStep==4)
    {
        // Open the lower side, move through it, then actually fire back from the exit.
        const double Moved=FVector::Dist2D(Feet(Victim),MudFoot);
        if (Moved>=175. && !Victim.IsMudCompact())
        {
            AimAt(2,true,Feet(*Attacker->GetHero())+FVector(0,0,95));
            UE_LOG(LogTemp,Display,TEXT("MUD_QA_ESCAPE travelled_cm=%.1f elapsed=%.2f"),Moved,Age);
            SetMudStep(5,Now);
            return;
        }
        if (Now-LastAction>.55)
        {
            const FVector Aim=FVector(0,1, (MudShots%3==1) ? -.42 : (MudShots%3==2 ? .26 : 0.)).GetSafeNormal();
            Peers[2]->ClientMudStroke(false,Aim);
            Peers[2]->ClientWalkToDig(MudFoot+FVector(0,215,0));
            LastAction=Now;
            ++MudShots;
        }
    }
    else if (MudStep==5)
    {
        if (Mode->GetMudMarks(*Attacker->GetHero())!=1)
        {
            if (Age>1. && Now-LastAction>1.) AimAt(2,true,Feet(*Attacker->GetHero())+FVector(0,0,90));
            return;
        }
        // A teammate excavates the shaft with the same shallow shovel as the player.
        // The second guest enters empty-handed and only uses throw and ordinary walking.
        Place(0,MudShaft+FVector(0,0,96));
        SetMudStep(6,Now);
    }
    else if (MudStep==6)
    {
        const auto Floor=MudGround(Terrain,MudShaft);
        if (!Floor || Terrain.IsBusy() || Age<.65) return;
        if (MudShaft.Z-Floor->Z<145.)
        {
            if (Now-LastAction > (bIssued ? 1.32 : .18))
            {
                bIssued = !bIssued;
                Peers[0]->ClientMudHold(false,bIssued,FRotator(-89.,0.,0.));
                LastAction = Now;
            }
            return;
        }
        if (bIssued) Peers[0]->ClientMudHold(false,false,FRotator(-89.,0.,0.));
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_SHAFT dig_depth_cm=%.1f"),MudShaft.Z-Floor->Z);
        MudBase=Floor->Z;
        Place(1,*Floor+FVector(0,0,96));
        MudShaft.Z=MudBase;
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_PIT prepared=1 bottom_z=%.1f"),MudBase);
        Place(0,*Floor+FVector(-225,-290,275));
        Mode->GetPlayers()[0]->ClientResetView(FRotator(-19.,52.,0.));
        SetMudStep(7,Now);
    }
    else if (MudStep==7)
    {
        const auto Floor=MudGround(Terrain,MudShaft);
        if (!Floor || Age<.7) return;
        if (bIssued && Floor->Z-MudBase>104. && FMath::Abs(Feet(*Attacker->GetHero()).Z-Floor->Z)<12.)
        {
            Peers[1]->ClientMudHold(true,false,FRotator(-89.,0.,0.));
            SetMudStep(8,Now);
            return;
        }
        if (!bIssued) { Peers[1]->ClientMudHold(true,true,FRotator(-89.,0.,0.)); bIssued=true; }
    }
    else if (MudStep==8)
    {
        const auto Floor=MudGround(Terrain,MudShaft);
        if (!Floor || !Shared(MudShaft)) return;
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_STEPS throws=3 rise_cm=%.1f peers=%d crc=%08x"),Floor->Z-MudBase,Reports.Num(),Terrain.GetChecksum());
        MudPhoto(TEXT("MudSteps"));
        SetMudStep(9,Now);
    }
    else if (MudStep==9)
    {
        const FVector Actual=Feet(*Attacker->GetHero());
        const auto Floor=MudGround(Terrain,Actual);
        if (Floor && FVector::Dist2D(Actual,MudShaft)>165. && FMath::Abs(Actual.Z-Floor->Z)<14. && Actual.Z-MudBase>=146.)
        {
            UE_LOG(LogTemp,Display,TEXT("MUD_QA_EXIT walked_cm=%.1f rise_cm=%.1f"),FVector::Dist2D(Actual,MudShaft),Actual.Z-MudBase);
            SetMudStep(10,Now);
            return;
        }
        // The natural lip is uneven. Try adjacent ways out with ordinary walking only.
        if (Now-LastAction<2.1) return;
        const FVector Dir=FRotator(0.,90.+45.*MudShots,0.).Vector();
        const auto Lip=MudGround(Terrain,MudShaft+Dir*205.);
        if (Lip) Peers[1]->ClientWalkToDig(*Lip);
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_LIP attempt=%d feet=%s gain=%.1f"),MudShots,*Actual.ToCompactString(),Actual.Z-MudBase);
        ++MudShots; LastAction=Now;
    }
    else if (MudStep==10)
    {
        // Re-run on exposed ground to distinguish immunity from a shot merely striking the roof.
        // A fresh, level site also ends the previous movement inputs, far outside their route.
        for (double X=-3200.; X<=3200.; X+=370.)
            for (double Y=-3200.; Y<=3200.; Y+=370.)
            {
                const auto V=MudGround(Terrain,{X,Y,0.});
                if (!V || FVector::Dist2D(*V,MudShaft)<1300. || FVector::Dist2D(*V,MudFoot)<1300.) continue;
                const auto A=MudGround(Terrain,*V+FVector(-220.,-85.,0.));
                const auto B=MudGround(Terrain,*V+FVector(-220.,85.,0.));
                if (!A || !B || FMath::Abs(A->Z-V->Z)>8. || FMath::Abs(B->Z-V->Z)>8.) continue;
                const FVector Headroom(0,0,95);
                if (GetWorld()->LineTraceTestByChannel(*A+Headroom,*V+Headroom,ECC_Visibility)
                    || GetWorld()->LineTraceTestByChannel(*B+Headroom,*V+Headroom,ECC_Visibility)) continue;
                Place(0,*A+FVector(0,0,96)); Place(1,*B+FVector(0,0,96)); Place(2,*V+FVector(0,0,96));
                SetMudStep(11,Now);
                return;
            }
        Require(false,TEXT("found clear lines for both teammates"));
    }
    else if (MudStep==11)
    {
        if (Age<.8) return;
        if (!bIssued)
        {
            const auto* First = CastChecked<AIslandPlayerController>(Mode->GetPlayers()[0]);
            const auto* Second = CastChecked<AIslandPlayerController>(Mode->GetPlayers()[1]);
            Require(First->GetTeam()==Second->GetTeam() && First->GetTeam()!=Target->GetTeam() && Mode->GetMudMarks(Victim)==0,
                TEXT("two teammates face a fresh opposing target"));
            UE_LOG(LogTemp,Display,TEXT("MUD_QA_ROSTER first=%s second=%s target=%s"),
                *First->GetName(),*Second->GetName(),*Target->GetName());
            const FVector To=Victim.GetActorLocation();
            AimAt(0,true,To); AimAt(1,true,To); bIssued=true;
        }
        else if (Age>1.65)
        {
            Require(Mode->GetMudMarks(Victim)==1,TEXT("two simultaneous hits give one shared mud mark"));
            SetMudStep(12,Now);
        }
    }
    else if (MudStep==12)
    {
        if (Mode->GetMudMarks(Victim)!=0) return;
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_STACK_CLEAR time=%.4f"),GetWorld()->GetTimeSeconds());
        SetMudStep(13,Now);
    }
    else if (MudStep==13)
    {
        if (Victim.IsMudStunned())
        {
            MudKnockTime=GetWorld()->GetTimeSeconds();
            SetMudStep(14,Now);
            return;
        }
        // Choose the next thrower from confirmed marks. A delayed or missed request
        // cannot substitute one teammate for the other when verifying a shared stack.
        if (Now-LastAction>.72 && MudShots<7)
            AimAt(Mode->GetMudMarks(Victim)%2,true,Victim.GetActorLocation());
    }
    else if (MudStep==14)
    {
        if (Victim.IsMudStunned()) return;
        Require(Victim.IsMudImmune(),TEXT("target receives the post-stun Gameplay Effect"));
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_WAKE time=%.4f"),GetWorld()->GetTimeSeconds());
        SetMudStep(15,Now);
    }
    else if (MudStep==15)
    {
        if (Age<3.5)
        {
            if (!Victim.IsMudImmune() || Victim.IsMudStunned() || Mode->GetMudMarks(Victim)!=0)
            {
                Require(false,TEXT("wake protection prevents pre-stacking"));
                return;
            }
            if (Now-LastAction>.55) AimAt(MudShots%2,true,Victim.GetActorLocation());
            return;
        }
        if (Victim.IsMudImmune()) return;
        Require(!Victim.IsMudStunned() && Mode->GetMudMarks(Victim)==0,TEXT("protected hits left no stored stack"));
        UE_LOG(LogTemp,Display,TEXT("MUD_QA_GROUP simultaneous_marks=1 expired=1 protected_throws=%d immunity_end_since_knock=%.2f end_time=%.4f"),
            MudShots,GetWorld()->GetTimeSeconds()-MudKnockTime,GetWorld()->GetTimeSeconds());
        SetMudStep(150,Now);
    }
    else if (MudStep==150)
    {
        if (!bIssued)
        {
            AimAt(1,true,Victim.GetActorLocation());
            bIssued=true;
            return;
        }
        if (Mode->GetMudMarks(Victim)!=1) return;
        Require(!Victim.IsMudImmune() && !Victim.IsMudStunned(),TEXT("the first actual hit after protection starts a new stack"));
        SetMudStep(16,Now);
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
