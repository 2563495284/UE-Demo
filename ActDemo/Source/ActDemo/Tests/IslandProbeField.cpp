#include "Tests/IslandProbe.h"

#include "Components/CapsuleComponent.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "HAL/FileManager.h"
#include "Island/ExpeditionMudBall.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Island/IslandWorld.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    constexpr int32 MinimumDigDepth = 28;
    constexpr int32 MinimumStandingDepth = 12;
    constexpr int32 SurfaceTolerance = 20;

    void FieldPhoto(const TCHAR* Label)
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("IslandScreenshot")))
        {
            FString Directory = FPaths::ScreenShotDir();
            FParse::Value(FCommandLine::Get(), TEXT("FieldShotsDir="), Directory);
            IFileManager::Get().MakeDirectory(*Directory, true);
            const FString Path = FPaths::Combine(Directory, FString(Label) + TEXT(".png"));
            FScreenshotRequest::RequestScreenshot(Path, true, false);
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_PHOTO file=%s"), *Path);
        }
    }

    bool FieldFloor(const AVoxelTerrain& Terrain, const FVector& XY, FVector& Result)
    {
        FHitResult Hit;
        if (!Terrain.TraceRock(XY + FVector(0,0,1600), XY - FVector(0,0,1600), nullptr, Hit)) return false;
        Result = Hit.ImpactPoint;
        return true;
    }

    TOptional<FVector> FindDigLanding(const AVoxelTerrain& Terrain, const AVoxelCharacter& Hero, const FVector& Before)
    {
        const auto& Capsule = *Hero.GetCapsuleComponent();
        const double HalfHeight = Capsule.GetScaledCapsuleHalfHeight();
        const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule.GetScaledCapsuleRadius(), HalfHeight);
        const FCollisionQueryParams Params(SCENE_QUERY_STAT(ExpeditionFieldLanding), false, &Hero);
        const FCollisionResponseParams Responses(Capsule.GetCollisionResponseToChannels());
        // The angled punch cuts a bowl past the crosshair. Locate the bottom with
        // a character-sized sweep so a narrow crevice cannot become a walk target.
        TOptional<FVector> Lowest;
        for (int32 X = -100; X <= 100; X += 20)
            for (int32 Y = -100; Y <= 100; Y += 20)
            {
                const FVector Center = Before + FVector(X,Y,0.);
                FHitResult Hit;
                if (!Terrain.GetWorld()->SweepSingleByChannel(Hit, Center + FVector(0,0,300.), Center - FVector(0,0,350.),
                    FQuat::Identity, Capsule.GetCollisionObjectType(), Shape, Params, Responses)
                    || Hit.GetActor() != &Terrain || Hit.bStartPenetrating || !Hero.GetCharacterMovement()->IsWalkable(Hit))
                    continue;
                const FVector Foot = Hit.Location - FVector(0,0,HalfHeight);
                if (Before.Z - Foot.Z >= MinimumStandingDepth && (!Lowest || Foot.Z < Lowest->Z)) Lowest = Foot;
            }
        return Lowest;
    }
}

int32 AIslandProbe::ExpeditionTeamForSeat(int32 Seat) const
{
    if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionMazeQA"))) return Seat / 2;
    if (bFieldScenario || bNeutralScenario || bMudScenario || FParse::Param(FCommandLine::Get(), TEXT("ExpeditionDecoyQA"))) return Seat < 2 ? 0 : 1;
    return bOneCrew ? 0 : Seat;
}

void AIslandProbePeer::ClientFieldAim_Implementation(FVector Target)
{
    if (auto* PC = Cast<AIslandPlayerController>(GetOwner()); PC && PC->GetHero())
    {
        PC->SetControlRotation((Target - PC->GetHero()->GetAimOrigin()).Rotation());
        FieldAim = Target;
    }
}

void AIslandProbePeer::ClientFieldKeepDigging_Implementation(bool bKeep, FVector Target)
{
    bKeepFieldDig = bKeep;
    if (bKeep)
        ClientFieldAim_Implementation(Target);
    else
        FieldAim.Reset();
}

void AIslandProbePeer::ClientFieldRally_Implementation()
{
    auto* PC = CastChecked<AIslandPlayerController>(GetOwner());
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::G, IE_Pressed, 1.f));
    PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::G, IE_Released, 0.f));
    UE_LOG(LogTemp, Display, TEXT("FIELD_QA_RALLY_INPUT"));
}

void AIslandProbePeer::TickFieldInput()
{
    TickFieldWalk(GetWorld()->GetDeltaSeconds());
    auto* PC = CastChecked<AIslandPlayerController>(GetOwner());
    auto* PS = PC->GetPlayerState<AIslandPlayerState>();
    if (PS)
    {
        const auto& Lock = PS->LockView;
        const int32 State = Lock.Chest ? 1 + (Lock.Team << 5) + (int32(Lock.Role) << 2)
            + (Lock.bAssisted ? 2 : 0) + (Lock.bSeized ? 1 : 0) : 0;
        if (LastFieldView != State)
        {
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_VIEW crew=%d lock=%d assisted=%d seized=%d role=%d"),
                PS->Team, Lock.Chest ? Lock.Team : INDEX_NONE, Lock.bAssisted, Lock.bSeized, int32(Lock.Role));
            LastFieldView = State;
        }
    }

    if (bFieldMouseDown && !bKeepFieldDig)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Released, 0.f));
        bFieldMouseDown = false;
    }
    if (!FieldAim.IsSet() || !PC->GetHero()) return;
    FVector Camera;
    FRotator Rotation;
    PC->GetPlayerViewPoint(Camera, Rotation);
    // The shoulder camera is offset from the capsule. Converge the crosshair on
    // the actual visible surface before submitting a physical left-button input.
    const FRotator Desired = (*FieldAim - Camera).Rotation();
    const FRotator Delta = (Desired - Rotation).GetNormalized();
    if (FMath::Abs(Delta.Yaw) > .7 || FMath::Abs(Delta.Pitch) > .7)
    {
        PC->SetControlRotation(PC->GetControlRotation() + Delta);
        return;
    }
    if (!bFieldMouseDown)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton, IE_Pressed, 1.f));
        bFieldMouseDown = true;
    }
    FieldAim.Reset();
    UE_LOG(LogTemp, Display, TEXT("FIELD_QA_PUNCH_INPUT"));
}

void AIslandProbePeer::ClientInspectDig_Implementation(FVector OriginalFloor, FVector ReferenceFloor, uint32 Sequence)
{
    const auto* Terrain = AVoxelTerrain::Find(GetWorld());
    if (!Terrain || Terrain->GetAppliedSequence() != Sequence) return;
    FVector Floor, Reference;
    if (!FieldFloor(*Terrain, OriginalFloor, Floor) || !FieldFloor(*Terrain, ReferenceFloor, Reference)) return;
    const int32 Drop = FMath::RoundToInt(OriginalFloor.Z - Floor.Z);
    const int32 ReferenceDelta = FMath::RoundToInt(ReferenceFloor.Z - Reference.Z);
    UE_LOG(LogTemp, Display, TEXT("FIELD_QA_TERRAIN seq=%u crc=%08x drop=%d reference_delta=%d"),
        Sequence, Terrain->GetChecksum(), Drop, ReferenceDelta);
    ServerReportDig(Sequence, Terrain->GetChecksum(), Drop, ReferenceDelta);
}

void AIslandProbePeer::ServerReportDig_Implementation(uint32 Sequence, uint32 Checksum, int32 Drop, int32 ReferenceDelta)
{
    if (AIslandProbe* Probe = Coordinator.Get()) Probe->RecordDig(Seat, Sequence, Checksum, Drop, ReferenceDelta);
}

void AIslandProbe::RecordDig(int32 Seat, uint32 Sequence, uint32 Checksum, int32 Drop, int32 ClientReferenceDelta)
{
    if (RoomCheck != ERoomCheck::Field || FieldStage != EFieldStep::InspectDig) return;
    const auto* Terrain = Mode->GetTerrain();
    Require(Sequence == Terrain->GetAppliedSequence() && Checksum == Terrain->GetChecksum()
        && Drop == FieldDrop && ClientReferenceDelta == ReferenceDelta, TEXT("dig and surrounding ground match on each peer"));
    Reports.Add(Seat);
}

void AIslandProbe::SetFieldStage(EFieldStep Stage, double Now)
{
    FieldStage = Stage;
    RoomCheck = ERoomCheck::Field;
    StepAt = Now;
    LastAction = Now;
    UE_LOG(LogTemp, Display, TEXT("FIELD_QA_STAGE step=%d"), int32(Stage));
}

void AIslandProbe::StartFieldCheck(double Now)
{
    const AVoxelTerrain& Terrain = *Mode->GetTerrain();
    if (ExpeditionPlayers != 3 || Terrain.GetSearchCircles().IsEmpty() || Terrain.GetAppliedSequence() != 0)
    {
        Require(false, TEXT("field probe starts on fresh terrain with a public search circle and three players"));
        return;
    }
    const auto& Circle = Terrain.GetSearchCircles()[0];
    const FVector Center(FVector2D(Circle.Center), 0.);
    const FVector Outward = Center.GetSafeNormal2D();
    const FVector Edge = Center + Outward * (Circle.Radius + 30.);
    const FVector Across = FVector::CrossProduct(FVector::UpVector, Outward);
    // Use the public search-area rim as an ordinary dig site. Measure a second point
    // four metres inland to check that an individual hit only modifies nearby ground.
    FVector ViewStand;
    if (!FieldFloor(Terrain, Edge, OriginalFloor) || !FieldFloor(Terrain, Edge - Outward * 400., ReferenceFloor)
        || !FieldFloor(Terrain, Edge + Outward * 88. - Across * 65., FieldStand)
        || !FieldFloor(Terrain, Edge - Outward * 90. + Across * 260., ViewStand))
    {
        Require(false, TEXT("guest and observer have solid ground beside the dig site"));
        return;
    }
    Place(1, FieldStand + FVector(0,0,95));
    Mode->GetPlayers()[1]->GetHero()->SetActorRotation(FRotator(0,(OriginalFloor - FieldStand).Rotation().Yaw,0));
    Place(0, ViewStand + FVector(0,0,95));
    auto* Host = Mode->GetPlayers()[0].Get();
    const FRotator View = (OriginalFloor - Host->GetHero()->GetAimOrigin()).Rotation();
    Host->GetHero()->SetActorRotation(FRotator(0,View.Yaw,0));
    Host->ClientResetView(View);
    SetFieldStage(EFieldStep::Aim, Now);
}

void AIslandProbe::BeginLockCheck(double Now)
{
    const FVector Chest = Mode->HiddenLayout.GetCenter(NextDig);
    const FVector Offsets[] = {{-30, 52, 62}, {-25,-48,62}, {46,0,55}};
    const auto& Players = Mode->GetPlayers();
    // Probe the actual excavated walls. The deeper vault can have an uneven rim on
    // one side; rotate all three poses together so the rival can still target either worker.
    for (const double Height : {0., 24., -16., 44.})
    {
        for (int32 Heading = 0; Heading < 24; ++Heading)
        {
            bool bReachable = true;
            for (int32 Seat = 0; Seat < Peers.Num(); ++Seat)
            {
                auto* PC = CastChecked<AIslandPlayerController>(Players[Seat]);
                auto* Hero = PC->GetHero();
                const FVector Offset = Offsets[Seat].RotateAngleAxis(15. * Heading, FVector::UpVector);
                Hero->TeleportTo(Chest + Offset + FVector(0., 0., Height), FRotator::ZeroRotator, false, true);
                Hero->GetCharacterMovement()->DisableMovement();
                bReachable &= PC->GetReachableChest() == Mode->Exposed[NextDig];
            }
            if (!bReachable) continue;
            auto* Rival = Players[2]->GetHero();
            const FCollisionQueryParams Query(SCENE_QUERY_STAT(FieldLockThrow), false, Rival);
            for (int32 Seat = 0; Seat < 2; ++Seat)
            {
                const FVector Target = Players[Seat]->GetHero()->GetActorLocation() + FVector(0.,0.,30.);
                bReachable &= !GetWorld()->SweepTestByChannel(Rival->GetAimOrigin(), Target, FQuat::Identity,
                    ECC_Visibility, FCollisionShape::MakeSphere(AExpeditionMudBall::Radius), Query);
            }
            if (!bReachable) continue;
            for (const auto& PC : Players)
                PC->ClientResetView((Chest - PC->GetHero()->GetAimOrigin()).Rotation());
            Require(true, TEXT("all three players can reach the vault and the rival can throw at each worker"));
            SetFieldStage(EFieldStep::Gather, Now);
            return;
        }
    }
    Require(false, TEXT("the excavated vault has three reachable poses and two clear mud trajectories"));
}

void AIslandProbe::TickFieldCheck(double Now)
{
    auto* Terrain = Mode->GetTerrain();
    const auto& Players = Mode->GetPlayers();
    const auto* Round = Mode->GetGameState<AIslandGameState>();
    const double Time = Round->GetServerWorldTimeSeconds();
    const double Age = Now - StepAt;
    if (Age > 22.)
    {
        if (FieldStage == EFieldStep::WalkToDig)
        {
            const AVoxelCharacter& Hero = *Players[1]->GetHero();
            const FVector Foot = Hero.GetActorLocation() - FVector(0,0,Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
            UE_LOG(LogTemp, Error, TEXT("FIELD_QA_WALK_TIMEOUT target=%s foot=%s xy_error=%.1f depth=%.1f advance=%.1f velocity=%s mode=%d"),
                *DigLanding.ToCompactString(), *Foot.ToCompactString(), FVector::Dist2D(Foot, DigLanding),
                OriginalFloor.Z - Foot.Z, FVector::Dist2D(Foot, FieldStand), *Hero.GetVelocity().ToCompactString(),
                int32(Hero.GetCharacterMovement()->MovementMode));
        }
        UE_LOG(LogTemp, Error, TEXT("ISLAND_QA_TIMEOUT field=%d"), int32(FieldStage));
        Require(false, TEXT("field interaction timed out"));
        return;
    }
    const auto& HostLock = Players[0]->GetPlayerState<AIslandPlayerState>()->LockView;
    const auto& PartnerLock = Players[1]->GetPlayerState<AIslandPlayerState>()->LockView;
    const auto& RivalLock = Players[2]->GetPlayerState<AIslandPlayerState>()->LockView;
    auto Rate = [Time](const auto& Lock) { return (Lock.ProgressAt(Time + .08) - Lock.ProgressAt(Time)) / .08; };
    // A rival sends the same right-button ability request used in play. Follow the
    // actual marks instead of assuming its client RPC, projectile and GAS effect are simultaneous.
    auto ThrowAtLockWorker = [&](int32 Seat, const FIslandLockView& Lock)
    {
        auto* Worker = Players[Seat]->GetHero();
        const int32 Marks = Mode->GetMudMarks(*Worker);
        const double Interval = Marks == 0 ? .14 : .63;
        if (Now - LastAction < Interval && LastAction != StepAt) return;
        if (Marks == 2)
        {
            Require(Lock.Chest && Lock.bAssisted && Lock.Team == 0 && !Lock.bSeized &&
                (Lock.Role == EIslandLockRole::Leader || Lock.Role == EIslandLockRole::Helper),
                TEXT("two physical mud hits leave both chest workers making progress"));
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_TWO_HITS worker=%d role=%d progress=%.3f"),
                Seat, int32(Lock.Role), Lock.ProgressAt(Time));
        }
        const FVector Origin = Players[2]->GetHero()->GetAimOrigin();
        const FVector Aim = (Worker->GetActorLocation() + FVector(0,0,30.) - Origin).GetSafeNormal();
        Peers[2]->ClientMudStroke(true, Aim);
        LastAction = Now;
    };
    switch (FieldStage)
    {
    case EFieldStep::Aim:
        if (Age > 1.3)
        {
            FieldPhoto(TEXT("Field01Surface"));
            Peers[1]->ClientFieldRally();
            SetFieldStage(EFieldStep::Strike, Now);
        }
        break;
    case EFieldStep::Strike:
    {
        if (Terrain->GetAppliedSequence() > 0)
        {
            FVector Floor, Reference;
            if (!FieldFloor(*Terrain, OriginalFloor, Floor) || !FieldFloor(*Terrain, ReferenceFloor, Reference))
            {
                Require(false, TEXT("ordinary digging leaves collision at the dig site and the reference point"));
                return;
            }
            FieldDrop = FMath::RoundToInt(OriginalFloor.Z - Floor.Z);
            ReferenceDelta = FMath::RoundToInt(ReferenceFloor.Z - Reference.Z);
            Require(Terrain->GetAppliedSequence() == 1 && FieldDrop >= MinimumDigDepth && FieldDrop <= 55,
                TEXT("one ordinary punch lowers the rock at its point of impact"));
            Require(FMath::Abs(ReferenceDelta) <= SurfaceTolerance, TEXT("ground four metres away retains its original height"));
            Reports.Reset();
            Players[0]->ClientResetView((Floor - Players[0]->GetHero()->GetAimOrigin()).Rotation());
            SetFieldStage(EFieldStep::InspectDig, Now);
        }
        else if (Now - LastAction > 2.)
        {
            Peers[1]->ClientFieldAim(OriginalFloor);
            LastAction = Now;
        }
        break;
    }
    case EFieldStep::InspectDig:
        if (Age > .65 && Reports.Num() == Players.Num())
        {
            Require(Terrain->GetAppliedSequence() == 1, TEXT("releasing the physical left mouse button stops the active dig ability"));
            FieldPhoto(TEXT("Field02Dig"));
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_SHARED peers=%d seq=%u crc=%08x drop=%d reference_delta=%d"),
                Reports.Num(), Terrain->GetAppliedSequence(), Terrain->GetChecksum(), FieldDrop, ReferenceDelta);
            const auto Landing = FindDigLanding(*Terrain, *Players[1]->GetHero(), OriginalFloor);
            if (!Landing)
            {
                Require(false, TEXT("the ordinary dig contains a walkable character-sized landing"));
                return;
            }
            DigLanding = Landing.GetValue();
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_WALK_TARGET depth=%.1f horizontal_offset=%.1f"),
                OriginalFloor.Z - DigLanding.Z, FVector::Dist2D(DigLanding, OriginalFloor));
            Peers[1]->ClientWalkToDig(DigLanding);
            SetFieldStage(EFieldStep::WalkToDig, Now);
        }
        else if (Now - LastAction > .3)
        {
            for (int32 Seat = 0; Seat < Peers.Num(); ++Seat)
                if (!Reports.Contains(Seat)) Peers[Seat]->ClientInspectDig(OriginalFloor, ReferenceFloor, Terrain->GetAppliedSequence());
            LastAction = Now;
        }
        break;
    case EFieldStep::WalkToDig:
    {
        const AVoxelCharacter& Hero = *Players[1]->GetHero();
        const FVector Foot = Hero.GetActorLocation() - FVector(0,0,Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
        if (FVector::DistSquared2D(Foot, DigLanding) < FMath::Square(55.) && FMath::Abs(Foot.Z - DigLanding.Z) < 45.
            && OriginalFloor.Z - Foot.Z >= MinimumStandingDepth && FVector::Dist2D(Foot, FieldStand) >= 65.
            && Hero.GetCharacterMovement()->IsMovingOnGround() && Hero.GetVelocity().SizeSquared() < FMath::Square(10.))
        {
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_WALK advance=%d depth=%d"),
                FMath::RoundToInt(FVector::Dist2D(Foot, FieldStand)), FMath::RoundToInt(OriginalFloor.Z - Foot.Z));
            Mode->RecoverPlayer(*Players[1]);
            // With up to 24 additional boxes, any buried chest can contribute a strong echo.
            const FVector Chest = Mode->HiddenLayout.GetCenter(1);
            for (int32 Heading = 0; Heading < 48; ++Heading)
            {
                const double Angle = Heading * UE_TWO_PI / 48.;
                FVector Stand;
                if (!FieldFloor(*Terrain, Chest + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.) * 530., Stand)) continue;
                const FVector Center(VoxelPlayground::QuantizeCenter(Stand));
                double Gap = MAX_dbl;
                for (int32 Index = 0; Index < Mode->HiddenLayout.Num(); ++Index)
                {
                    if (Mode->Exposed[Index] || !Mode->CanClaimChest(0, Index)) continue;
                    const FBox Bounds = FBox::BuildAABB(Mode->HiddenLayout.GetCenter(Index), IslandRules::ChestExtent);
                    const double Distance = FMath::Sqrt(Bounds.ComputeSquaredDistanceToPoint(Center));
                    Gap = FMath::Min(Gap, Distance - VoxelPlayground::RadiusFor(VoxelPlayground::ShallowCut));
                }
                if (Gap < 310. || Gap > 490.) continue;
                Place(0, Stand + FVector(0,0,96.));
                UE_LOG(LogTemp, Display, TEXT("FIELD_QA_ECHO_SEARCH predicted_gap=%.1f"), Gap);
                SetFieldStage(EFieldStep::EchoApproach, Now);
                return;
            }
            Require(false, TEXT("missing shallow search column for proximity echo"));
        }
        break;
    }
    case EFieldStep::EchoApproach:
        if (Age > .45)
        {
            Players[0]->SubmitEdit(EVoxelAction::Punch, -FVector::UpVector);
            SetFieldStage(EFieldStep::HearEcho, Now);
        }
        break;
    case EFieldStep::HearEcho:
    {
        const auto* PC = CastChecked<AIslandPlayerController>(Players[0]);
        if (PC->GetEchoAge() < 1.)
        {
            Require(!PC->HasStrongEcho(), TEXT("a distant dig gives a weak echo to its excavator"));
            // This probe exercises the final vault and physical lock interference. Its starting
            // inventory is a fixture; the games probe obtains the fragments through live arenas.
            for (int32 Site = 0; Site < IslandRules::ClueCount; ++Site) Mode->GiveClue(0, Site);
            for (int32 Site = 0; Site < IslandRules::ClueCount - 1; ++Site) Mode->GiveClue(1, Site);
            Require(Mode->TeamClues[0].CanOpenPrize() && Mode->TeamClues[1].HasKey()
                && !Mode->TeamClues[1].CanOpenPrize() && Mode->TeamClues[1].Crossing.IsNearlyZero(),
                TEXT("both crews have a key but only the crew with four sites can locate and open the vault"));
            RoomCheck = ERoomCheck::Dig;
            PrepareDig(IslandRules::PrizeIndex);
        }
        break;
    }
    case EFieldStep::Gather:
        if (Age > .6 && LastAction == StepAt)
        {
            Peers[2]->ClientOpenChest();
            LastAction = Now;
        }
        else if (LastAction != StepAt && Now - LastAction > .65)
        {
            Require(!RivalLock.Chest && Mode->ChestLocks.IsEmpty() && !Mode->TeamClues[1].CanOpenPrize(),
                TEXT("a physically present rival with a key and three sites cannot start the vault"));
            Mode->GiveClue(1, IslandRules::ClueCount - 1);
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_VAULT_KEYS three_rejected=1 crews=2"));
            Peers[1]->ClientOpenChest();
            SetFieldStage(EFieldStep::Open, Now);
        }
        break;
    case EFieldStep::Open:
        if (PartnerLock.Chest && PartnerLock.Role == EIslandLockRole::Leader && PartnerLock.Team == 0)
        {
            FieldSoloRate = Rate(PartnerLock);
            Require(FieldSoloRate > 0., TEXT("one player makes positive opening progress"));
            Peers[0]->ClientOpenChest();
            SetFieldStage(EFieldStep::Assist, Now);
        }
        break;
    case EFieldStep::Assist:
        if (HostLock.Chest && HostLock.Role == EIslandLockRole::Helper && HostLock.bAssisted && RivalLock.bAssisted)
        {
            Require(Rate(HostLock) > FieldSoloRate * 1.8, TEXT("two players accelerate actual lock progress"));
            Peers[2]->ClientOpenChest();
            SetFieldStage(EFieldStep::Defend, Now);
        }
        break;
    case EFieldStep::Defend:
        if (Players[1]->GetHero()->IsMudStunned())
        {
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_STUN_LOCK worker=leader next=helper"));
            SetFieldStage(EFieldStep::TakeOver, Now);
        }
        else
        {
            Require(HostLock.Team == 0 && HostLock.bAssisted && !HostLock.bSeized, TEXT("opponent cannot seize a crewed lock"));
            if (LastAction == StepAt) FieldPhoto(TEXT("Field03Assist"));
            ThrowAtLockWorker(1, PartnerLock);
        }
        break;
    case EFieldStep::TakeOver:
        if (HostLock.Chest && HostLock.Role == EIslandLockRole::Leader && !HostLock.bAssisted && HostLock.Team == 0)
        {
            Require(Rate(HostLock) > 0. && Rate(HostLock) < FieldSoloRate * 1.2, TEXT("helper inherits work at solo speed after leader leaves"));
            Peers[2]->ClientOpenChest();
            SetFieldStage(EFieldStep::Seize, Now);
        }
        break;
    case EFieldStep::Seize:
        if (RivalLock.Chest && RivalLock.Team == 1 && RivalLock.Role == EIslandLockRole::Leader && RivalLock.bSeized)
        {
            Require(RivalLock.ProgressAt(Time) >= .49, TEXT("seizing crew inherits half the opening progress"));
            Peers[0]->ClientOpenChest();
            SetFieldStage(EFieldStep::PreventReseize, Now);
        }
        break;
    case EFieldStep::PreventReseize:
        if (Age > .35)
        {
            Require(HostLock.Team == 1 && HostLock.bSeized && RivalLock.Role == EIslandLockRole::Leader,
                TEXT("former crew cannot seize the same attempt back"));
            FieldPhoto(TEXT("Field04Seized"));
            // Leave before the prize opens, so the same final vault can exercise a second attempt.
            Mode->RecoverPlayer(*Players[2]);
            SetFieldStage(EFieldStep::Award, Now);
        }
        break;
    case EFieldStep::Award:
        if (!HostLock.Chest && !PartnerLock.Chest && !RivalLock.Chest && !Players[1]->GetHero()->IsMudStunned())
        {
            Require(Mode->TeamClues[0].HasKey() && Mode->TeamClues[1].HasKey()
                && Round->Winner == INDEX_NONE, TEXT("abandoning a contested final vault preserves both keys"));
            BeginLockCheck(Now);
            SetFieldStage(EFieldStep::Cancel, Now);
        }
        break;
    case EFieldStep::Cancel:
        if (Players[0]->GetHero()->IsMudStunned())
        {
            Require(PartnerLock.Chest && PartnerLock.Role == EIslandLockRole::Leader &&
                !PartnerLock.bAssisted && PartnerLock.Team == 0 && Rate(PartnerLock) > 0.,
                TEXT("the active leader keeps working at solo speed when the helper is stunned"));
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_STUN_LOCK worker=helper next=leader"));
            Mode->RecoverPlayer(*Players[1]);
            SetFieldStage(EFieldStep::Reset, Now);
        }
        else if (HostLock.Role == EIslandLockRole::Helper && HostLock.bAssisted)
        {
            ThrowAtLockWorker(0, HostLock);
        }
        else if (!PartnerLock.Chest && Age > .6 && Now - LastAction > .35)
        {
            Peers[1]->ClientOpenChest();
            LastAction = Now;
        }
        else if (PartnerLock.Role == EIslandLockRole::Leader && !PartnerLock.bAssisted &&
            (LastAction == StepAt || Now - LastAction > .25))
        {
            Require(PartnerLock.ProgressAt(Time) < .25, TEXT("new attempt starts without inherited lock or progress"));
            Peers[0]->ClientOpenChest();
            LastAction = Now;
        }
        break;
    case EFieldStep::Reset:
        if (Age > .5 && !RivalLock.Chest && !HostLock.Chest && !PartnerLock.Chest)
        {
            Require(Mode->TeamClues[0].HasKey() && Mode->TeamClues[1].HasKey() && Round->Winner == INDEX_NONE,
                TEXT("the second abandonment also preserves each crew's assembled key"));
            Reports.Reset();
            ++Epoch;
            Step = EStep::Inspect;
            ExpectedDigest.Reset();
            SetFieldStage(EFieldStep::Verify, Now);
        }
        break;
    case EFieldStep::Verify:
        if (Reports.Num() == Players.Num())
        {
            const FVector Ahead = (OriginalFloor - FieldStand).GetSafeNormal2D();
            const FVector Side = FVector::CrossProduct(FVector::UpVector, Ahead);
            FVector Stand;
            if (!FieldFloor(*Terrain, ReferenceFloor + Side*120., SecondFieldTarget)
                || !FieldFloor(*Terrain, ReferenceFloor - Ahead*90. + Side*60., Stand))
            {
                Require(false, TEXT("two visible terrain targets have a nearby standing surface"));
                return;
            }
            Place(1, Stand + FVector(0,0,95.));
            HoldSequence = Terrain->GetAppliedSequence();
            SetFieldStage(EFieldStep::AimHeldDig, Now);
        }
        else if (Now - LastAction > .35)
        {
            for (int32 Seat = 0; Seat < Peers.Num(); ++Seat)
                if (!Reports.Contains(Seat)) Peers[Seat]->ClientInspect(Epoch, false);
            LastAction = Now;
        }
        break;
    case EFieldStep::AimHeldDig:
        if (Age > .6)
        {
            Peers[1]->ClientFieldKeepDigging(true, ReferenceFloor);
            SetFieldStage(EFieldStep::HoldFirst, Now);
        }
        break;
    case EFieldStep::HoldFirst:
        if (Terrain->GetAppliedSequence() > HoldSequence && Age > .65)
        {
            FVector Floor;
            Require(FieldFloor(*Terrain, ReferenceFloor, Floor) && ReferenceFloor.Z - Floor.Z >= MinimumDigDepth,
                TEXT("holding real left mouse excavates the first crosshair target"));
            const auto* Dig = Players[1]->GetHero()->GetAbilitySystemComponent()->FindAbilitySpecFromClass(UExpeditionDigAbility::StaticClass());
            Require(Dig && Dig->IsActive(), TEXT("held remote input has an active server dig ability"));
            Peers[1]->ClientFieldKeepDigging(true, SecondFieldTarget);
            SetFieldStage(EFieldStep::HoldSecond, Now);
        }
        break;
    case EFieldStep::HoldSecond:
    {
        FVector Floor;
        if (FieldFloor(*Terrain, SecondFieldTarget, Floor) && SecondFieldTarget.Z - Floor.Z >= MinimumDigDepth)
        {
            Peers[1]->ClientFieldKeepDigging(false, FVector::ZeroVector);
            HoldSequence = Terrain->GetAppliedSequence();
            SetFieldStage(EFieldStep::Released, Now);
        }
        break;
    }
    case EFieldStep::Released:
        // Give the reliable remote release time to arrive, then inspect for longer
        // than a full 0.50 s swing interval.
        if (Age < .3) HoldSequence = Terrain->GetAppliedSequence();
        if (Age > 1.15)
        {
            const auto* Dig = Players[1]->GetHero()->GetAbilitySystemComponent()->FindAbilitySpecFromClass(UExpeditionDigAbility::StaticClass());
            Require(Dig && !Dig->IsActive() && Terrain->GetAppliedSequence()==HoldSequence,
                TEXT("mouse release cancels the server ability and leaves no queued swing"));
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_HELD physical=1 repeated=1 retargeted=1 released=1 sequence=%u"), HoldSequence);
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_DONE players=%d assisted=1 protected=1 promoted=1 seized=1 single_seizure=1 cancelled=1 private=1"), Players.Num());
            Advance(EStep::Done);
        }
        break;
    }
}
