#include "Tests/IslandProbe.h"

#include "Components/CapsuleComponent.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "HAL/FileManager.h"
#include "Island/ExpeditionMudBall.h"
#include "Island/ExpeditionBodyMud.h"
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

void AIslandProbePeer::ClientFieldEchoStrike_Implementation(int32 Trial)
{
    UE_LOG(LogTemp, Display, TEXT("FIELD_QA_ECHO_STRIKE trial=%d"), Trial);
    CastChecked<AIslandPlayerController>(GetOwner())->SubmitEdit(EVoxelAction::Punch, -FVector::UpVector);
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

bool AIslandProbe::BeginLockCheck(double Now)
{
    const auto& Players = Mode->GetPlayers();
    auto* Vault = Mode->Exposed[NextDig].Get();
    auto* Host = CastChecked<AIslandPlayerController>(Players[0]);
    auto* Hero = Host->GetHero();
    const auto* Capsule = Hero->GetCapsuleComponent();
    const auto* Terrain = Mode->GetTerrain();
    const FVector Chest = Vault->GetActorLocation();
    const FVector Lid = Chest + IslandRules::ClosedLidCenter;
    const FVector EyeOffset = Hero->GetAimOrigin() - Hero->GetActorLocation();
    const FVector StartPose = Hero->GetActorLocation();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(FieldLockPoses), false, Hero);
    const FCollisionShape Shape = Capsule->GetCollisionShape();
    const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());

    // Record a real line of sight on the first exposed frame, before enlarging the pit for the contest.
    const auto SeeFirstLid = [&](FVector Eye)
    {
        if (bFieldLidSighted) return;
        for (double X = -44.; X <= 44.; X += 11.)
            for (double Y = -32.; Y <= 32.; Y += 8.)
            {
                const FVector End = Lid + FVector(X,Y,-5.);
                FHitResult Sight;
                if (!GetWorld()->LineTraceSingleByChannel(Sight, Eye, End, ECC_Visibility, Query) || Sight.GetActor() != Vault) continue;
                bFieldLidSighted = true;
                const bool bCovered = !Terrain->IsVolumeClear(FBox::BuildAABB(Chest, IslandRules::ChestExtent));
                UE_LOG(LogTemp, Display, TEXT("FIELD_QA_FIRST_LID sight=1 body_covered=%d distance_cm=%.1f"), bCovered, Sight.Distance);
                return;
            }
    };
    SeeFirstLid(Hero->GetAimOrigin());

    TArray<FVector> Stands;
    for (double Radius : {45., 75., 105., 135., 165.})
    {
        for (int32 Heading = 0; Heading < 24; ++Heading)
        {
            const double Angle = Heading * UE_TWO_PI / 24.;
            const FVector Column = Chest + Radius * FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.);
            FHitResult Floor;
            if (!GetWorld()->SweepSingleByChannel(Floor, Column + FVector(0,0,1400.), Column - FVector(0,0,900.),
                FQuat::Identity, Capsule->GetCollisionObjectType(), Shape, Query, Responses)
                || Floor.bStartPenetrating || !Hero->GetCharacterMovement()->IsWalkable(Floor)) continue;
            const FVector Stand = Floor.Location + FVector(0,0,3.);
            const FVector Eye = Stand + EyeOffset;
            SeeFirstLid(Eye);
            if (Vault->GetInteractionDistanceSquared(Eye) > FMath::Square(IslandRules::OpenReach)) continue;
            Hero->TeleportTo(Stand, FRotator::ZeroRotator, false, true);
            FVector Target;
            if (Vault->IsReachableFrom(*Hero,&Target) && Host->GetChestAlongAim((Target-Eye).GetSafeNormal()) == Vault)
                Stands.Add(Stand);
        }
    }
    Hero->TeleportTo(StartPose, FRotator::ZeroRotator, false, true);
    if (!bFieldLidSighted) { Require(false, TEXT("the newly exposed lid is visible before any further punch")); return false; }

    for (const FVector Rival : Stands)
    {
        TArray<FVector> Workers;
        for (const FVector Stand : Stands)
        {
            const double Separation = FVector::Dist2D(Rival, Stand);
            if (Separation < 82. || Separation > 185. || FMath::Abs(Rival.Z-Stand.Z)>65.) continue;
            if (GetWorld()->SweepTestByChannel(Rival+EyeOffset, Stand+FVector(0,0,30.), FQuat::Identity,
                ECC_Visibility, FCollisionShape::MakeSphere(AExpeditionMudBall::Radius), Query)) continue;
            // Keep both capsules outside the throw to the other worker, so misses in the lock test are meaningful.
            for (const FVector First : Workers)
            {
                if (FVector::Dist2D(First,Stand)<82. || FMath::PointDistToSegment(First,Rival,Stand)<68.
                    || FMath::PointDistToSegment(Stand,Rival,First)<68.) continue;
                const FVector Locations[] = {First, Stand, Rival};
                for (int32 Seat=0; Seat<3; ++Seat)
                {
                    auto* PC = CastChecked<AIslandPlayerController>(Players[Seat]);
                    PC->GetHero()->TeleportTo(Locations[Seat],FRotator::ZeroRotator,false,true);
                    PC->GetHero()->GetCharacterMovement()->DisableMovement();
                    FVector Target;
                    if (Seat!=1 && Vault->IsReachableFrom(*PC->GetHero(),&Target))
                        PC->ClientResetView((Target-PC->GetHero()->GetAimOrigin()).Rotation());
                }
                Require(true, TEXT("all three ground poses can aim at the exposed vault and the rival can throw at either worker"));
                if (!Peers[1]->bHeldLidObserved)
                {
                    Peers[1]->ClientWatchHeldLid();
                    SetFieldStage(EFieldStep::HoldThroughReveal, Now);
                }
                return true;
            }
            Workers.Add(Stand);
        }
    }

    // A single visible corner does not guarantee standing room for three capsules. Widen
    // only the soil around the sides by punching from the ground above that column.
    TOptional<FVector> Cut;
    for (double Radius : {90.,130.})
        for (int32 Heading=0; Heading<12; ++Heading)
        {
            const double Angle = Heading*UE_TWO_PI/12.;
            const FVector Column = Chest + Radius*FVector(FMath::Cos(Angle),FMath::Sin(Angle),0.);
            FHitResult Floor;
            if (!Terrain->TraceRock(Column+FVector(0,0,1500.),Column-FVector(0,0,1100.),Hero,Floor)
                || Floor.ImpactPoint.Z <= Chest.Z-35.) continue;
            if (!Cut || Floor.ImpactPoint.Z < Cut->Z) Cut=Floor.ImpactPoint;
        }
    if (Cut && Now-StepAt<98.)
    {
        Hero->TeleportTo(*Cut+FVector(0,0,105.),FRotator::ZeroRotator,false,true);
        Hero->GetCharacterMovement()->DisableMovement();
        Host->SubmitEdit(EVoxelAction::Punch,-FVector::UpVector);
        UE_LOG(LogTemp,Display,TEXT("FIELD_QA_WIDEN_VAULT previous_stands=%d point=%s"),Stands.Num(),*Cut->ToCompactString());
        LastAction=Now;
        return false;
    }
    Require(false, TEXT("widening the vault rim makes room for three visible lock poses"));
    return false;
}

void AIslandProbe::PlaceFieldEchoDig(double Now)
{
    const auto& Layout = Mode->HiddenLayout;
    const auto* Terrain = Mode->GetTerrain();
    const double Radii[] = {680., 400., 130., 400., 130.};
    const bool bDecoy = FieldEchoTrial >= 3;
    const bool bFirstOfKind = FieldEchoTrial == 0 || FieldEchoTrial == 3;
    for (int32 Site = 0; Site < Layout.Num(); ++Site)
    {
        if (Site == IslandRules::PrizeIndex || Layout.IsDecoy(Site) != bDecoy
            || (!bFirstOfKind && Site != FieldEchoTarget) || Mode->Exposed[Site] || !Mode->CanClaimChest(0, Site)) continue;
        for (int32 Heading = 0; Heading < 48; ++Heading)
        {
            const double Angle = (Heading + FieldEchoTrial * 16) * UE_TWO_PI / 48.;
            FVector Floor;
            if (!FieldFloor(*Terrain, Layout.GetCenter(Site) + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.) * Radii[FieldEchoTrial], Floor)) continue;
            const FVector Dig(VoxelPlayground::QuantizeCenter(Floor));
            // Independently measure the three-dimensional distance from this cut to each eligible lid.
            // Test both kinds on the real layout, including locations where their hearing ranges overlap.
            double Nearest = MAX_dbl;
            int32 NearestSite = INDEX_NONE, Audible = 0;
            FVector Direction = FVector::ZeroVector;
            for (int32 Index = 0; Index < Layout.Num(); ++Index)
            {
                if (Mode->Exposed[Index] || !Mode->CanClaimChest(0, Index)) continue;
                const FVector Delta = Layout.GetCenter(Index) + FVector(0, 0, 25.) - Dig;
                const double Distance = Delta.Length();
                if (Distance < 1000.) ++Audible;
                if (Distance >= Nearest) continue;
                NearestSite = Index;
                Nearest = Distance;
                Direction = Delta.GetSafeNormal();
            }
            if (NearestSite != Site || Nearest > Radii[FieldEchoTrial] + 120.) continue;
            Place(1, Floor + FVector(0, 0, 103.));
            auto* Hero = Mode->GetPlayers()[1]->GetHero();
            if (FVector::Dist2D(Hero->GetActorLocation(), Floor) > 3.) continue;
            Hero->GetCharacterMovement()->DisableMovement();
            FieldEchoTarget = Site;
            FieldEchoSequence = Terrain->GetAppliedSequence();
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_ECHO_EXPECT trial=%d site=%d decoy=%d candidates=%d distance_cm=%.1f direction=%.4f,%.4f,%.4f"),
                FieldEchoTrial, Site, bDecoy, Audible, Nearest, Direction.X, Direction.Y, Direction.Z);
            SetFieldStage(EFieldStep::EchoApproach, Now);
            return;
        }
    }
    Require(false, TEXT("a reachable search column for the directional echo trial"));
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
    // Front-load the torso throws before starting the four-second vault clock, so
    // editor frame rate and render readback cannot turn this test into a lock-time race.
    const auto AnchorWorker = [&](int32 Seat, const FIslandLockView& Lock)
    {
        auto* Worker = Players[Seat]->GetHero();
        Require(Worker->GetBodyMud()->Num() >= 2 && Lock.Chest && Lock.bAssisted && Lock.Team == 0 && !Lock.bSeized,
            TEXT("two physical torso hits still allow full assisted chest progress"));
        UE_LOG(LogTemp, Display, TEXT("FIELD_QA_TWO_HITS worker=%d role=%d progress=%.3f"), Seat, int32(Lock.Role), Lock.ProgressAt(Time));
        // Keep the client looking at its real vault. Submit an ankle impact geometrically,
        // then allow the body component to test the actual supporting surface.
        const auto* Capsule = Worker->GetCapsuleComponent();
        const FVector Position = Capsule->GetComponentLocation();
        FHitResult Floor;
        const bool Grounded = GetWorld()->SweepSingleByChannel(Floor,Position+FVector::UpVector*6.,Position-FVector::UpVector*14.,
            FQuat::Identity,Capsule->GetCollisionObjectType(),Capsule->GetCollisionShape(),
            FCollisionQueryParams(SCENE_QUERY_STAT(FieldAnkle),false,Worker),FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
        Require(Grounded && !Floor.bStartPenetrating,TEXT("the chest worker has a real standing contact"));
        if (!Grounded || Floor.bStartPenetrating) return;
        // On an inclined rim the capsule meets the ground on the side of its lower hemisphere.
        const FVector Contact = Floor.ImpactPoint + Position - Floor.Location;
        const FHitResult Ankle(Worker,Worker->GetCapsuleComponent(),Contact,-Floor.ImpactNormal);
        Mode->ResolveMudImpact(*CastChecked<AIslandPlayerController>(Players[2]),Ankle);
        Require(Worker->IsMudAnchored(),TEXT("the ankle deposit actually connects the worker to the floor"));
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
            PlaceFieldEchoDig(Now);
        }
        break;
    }
    case EFieldStep::EchoApproach:
        if (Age > .65)
        {
            Peers[1]->ClientFieldEchoStrike(FieldEchoTrial);
            SetFieldStage(EFieldStep::HearEcho, Now);
        }
        break;
    case EFieldStep::HearEcho:
    {
        if (Terrain->GetAppliedSequence() > FieldEchoSequence && Age > .65)
        {
            ++FieldEchoTrial;
            if (FieldEchoTrial < (Mode->HiddenLayout.Decoys.IsEmpty() ? 3 : 5))
            {
                PlaceFieldEchoDig(Now);
                return;
            }
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_ECHO_END trials=%d"), FieldEchoTrial);
            Mode->RecoverPlayer(*Players[1]);
            // This probe exercises the final vault and physical lock interference. Its starting
            // inventory is a fixture; the games probe obtains the fragments through live arenas.
            for (int32 Site = 0; Site < IslandRules::ClueCount; ++Site) Mode->GiveClue(0, Site);
            for (int32 Site = 0; Site < IslandRules::ClueCount - 1; ++Site) Mode->GiveClue(1, Site);
            Require(Mode->TeamClues[0].CanOpenPrize() && Mode->TeamClues[1].HasKey()
                && !Mode->TeamClues[1].CanOpenPrize() && Mode->TeamClues[1].Crossing.IsNearlyZero(),
                TEXT("both crews have a key but only the crew with four sites can locate and open the vault"));
            RoomCheck = ERoomCheck::Dig;
            PrepareDig(IslandRules::PrizeIndex);
            Place(1, Players[0]->GetHero()->GetActorLocation() + FVector(20., 0., 0.));
            // The coordinator also digs below this witness. Keep the sample pose at
            // the shaft rim instead of letting an off-island fall reset its held tool.
            Players[1]->GetHero()->GetCharacterMovement()->DisableMovement();
            Peers[1]->ClientMudHold(false, true, FRotator(-82.,0.,0.));
        }
        break;
    }
    case EFieldStep::HoldThroughReveal:
        if (Peers[1]->bHeldLidObserved)
        {
            const bool bHeld = Players[1]->IsHoldingExpeditionDig();
            const bool bUntouched = Mode->ChestLocks.IsEmpty() && Round->Phase == EIslandPhase::Hunting;
            Require(bHeld && bUntouched,
                TEXT("keeping the same left press through excavation and aiming at the revealed lid never opens it"));
            UE_LOG(LogTemp,Display,TEXT("FIELD_QA_LID_HELD physical=%d exposed=1 aimed=1 locked=%d"),bHeld,!bUntouched);
            Peers[1]->ClientMudHold(false,false,Players[1]->GetControlRotation());
            SetFieldStage(EFieldStep::Gather, Now);
        }
        break;
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
            SetFieldStage(EFieldStep::CoatLeader, Now);
        }
        break;
    case EFieldStep::CoatLeader:
    case EFieldStep::CoatHelper:
    {
        const bool Leader = FieldStage == EFieldStep::CoatLeader;
        const auto* Worker = Players[Leader ? 1 : 0]->GetHero();
        if (Worker->GetBodyMud()->Num() >= 2)
        {
            Require(!Worker->IsMudAnchored(),TEXT("torso mud by itself does not anchor the worker"));
            if (Leader) Peers[1]->ClientOpenChest();
            SetFieldStage(Leader ? EFieldStep::Open : EFieldStep::Cancel, Now);
        }
        else if (Now-LastAction>.75)
        {
            const FVector Eye = Players[2]->GetHero()->GetAimOrigin();
            const FVector Aim = AExpeditionMudBall::AimAtPoint(Eye,Worker->GetActorLocation()+FVector(0,0,30.));
            Peers[2]->ClientMudStroke(true,Aim);
            LastAction = Now;
        }
        break;
    }
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
        if (Players[1]->GetHero()->IsMudAnchored())
        {
            FieldPeelInitial=Players[1]->GetHero()->GetBodyMud()->Num();
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_ANCHORED_LOCK worker=leader next=helper"));
            SetFieldStage(EFieldStep::TakeOver, Now);
        }
        else
        {
            Require(HostLock.Team == 0 && HostLock.bAssisted && !HostLock.bSeized, TEXT("opponent cannot seize a crewed lock"));
            if (LastAction == StepAt) FieldPhoto(TEXT("Field03Assist"));
            if (Age > .48) AnchorWorker(1, PartnerLock);
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
        if (Age > .35 && Peers[0]->HasReleasedChestClick())
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
        if (HostLock.Chest || PartnerLock.Chest || RivalLock.Chest)
        {
            if (FieldPeelPrevious != INDEX_NONE)
                Require(false,TEXT("a self-peel at the lid cannot reopen the abandoned chest"));
            break;
        }
        if (Players[1]->GetHero()->IsMudAnchored())
        {
            const int32 Count=Players[1]->GetHero()->GetBodyMud()->Num();
            if (Age<.35 || Now-LastAction<.7) break;
            if (Count!=FieldPeelPrevious)
            {
                UE_LOG(LogTemp,Display,TEXT("FIELD_QA_PEEL_PRESS remaining=%d anchored=1"),Count);
                FieldPeelPrevious=Count;
                Peers[1]->ClientOpenChest(); // Real press/release at the eligible visible vault lid.
                LastAction=Now;
            }
            else if (Now-LastAction>2.)
            {
                UE_LOG(LogTemp,Error,TEXT("FIELD_QA_PEEL_STALLED remaining=%d initial=%d"),Count,FieldPeelInitial);
                Require(false,TEXT("clicking the lid while bound peels an actual clod"));
            }
            break;
        }
        if (Age>.5 && Now-LastAction>.65 && !Terrain->IsBusy())
        {
            const int32 Remaining=Players[1]->GetHero()->GetBodyMud()->Num();
            Require(FieldPeelPrevious!=INDEX_NONE && Remaining<FieldPeelInitial,TEXT("the vault-side physical click peeled clay and left the lock closed"));
            Require(Mode->TeamClues[0].HasKey() && Mode->TeamClues[1].HasKey()
                && Round->Winner==INDEX_NONE,TEXT("abandoning a contested final vault preserves both keys"));
            UE_LOG(LogTemp,Display,TEXT("FIELD_QA_PEEL_AT_CHEST released=1 assisted=0 before=%d remaining=%d"),FieldPeelInitial,Remaining);
            if (BeginLockCheck(Now)) SetFieldStage(EFieldStep::CoatHelper,Now);
        }
        break;
    case EFieldStep::Cancel:
        if (Players[0]->GetHero()->IsMudAnchored())
        {
            Require(PartnerLock.Chest && PartnerLock.Role == EIslandLockRole::Leader &&
                !PartnerLock.bAssisted && PartnerLock.Team == 0 && Rate(PartnerLock) > 0.,
                TEXT("the active leader keeps working at solo speed when the helper is connected to the floor"));
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_ANCHORED_LOCK worker=helper next=leader"));
            Mode->RecoverPlayer(*Players[1]);
            SetFieldStage(EFieldStep::Reset, Now);
        }
        else if (HostLock.Role == EIslandLockRole::Helper && HostLock.bAssisted)
        {
            AnchorWorker(0, HostLock);
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
            if (!FieldFloor(*Terrain, ReferenceFloor + Side*172., SecondFieldTarget)
                || !FieldFloor(*Terrain, ReferenceFloor - Ahead*84. + Side*86., Stand))
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
        if (Players[1]->IsHoldingExpeditionDig() && Age > .7)
        {
            Require(Terrain->GetAppliedSequence()==HoldSequence,TEXT("holding a physical punch spends no edits"));
            Peers[1]->ClientFieldKeepDigging(true,SecondFieldTarget);
            SetFieldStage(EFieldStep::HoldSecond,Now);
        }
        break;
    case EFieldStep::HoldSecond:
        if (Age > 1.3)
        {
            Require(Terrain->GetAppliedSequence()==HoldSequence,TEXT("holding beyond the charge cap still spends no edits"));
            Peers[1]->ClientFieldKeepDigging(false,FVector::ZeroVector);
            SetFieldStage(EFieldStep::Released,Now);
        }
        break;
    case EFieldStep::Released:
        if (Age > 1.15 && Terrain->GetAppliedSequence()>HoldSequence)
        {
            FVector Floor, OriginalAimFloor;
            const auto* Dig = Players[1]->GetHero()->GetAbilitySystemComponent()->FindAbilitySpecFromClass(UExpeditionDigAbility::StaticClass());
            Require(Dig && !Dig->IsActive() && Terrain->GetAppliedSequence()==HoldSequence+1 &&
                FieldFloor(*Terrain,SecondFieldTarget,Floor) && SecondFieldTarget.Z-Floor.Z >= MinimumDigDepth &&
                FieldFloor(*Terrain,ReferenceFloor,OriginalAimFloor) && FMath::Abs(OriginalAimFloor.Z-ReferenceFloor.Z)<8.,
                TEXT("release strikes once at the final crosshair target and leaves no automatic swings"));
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_HELD physical=1 held_edits=0 retargeted=1 released_edits=1 sequence=%u"), Terrain->GetAppliedSequence());
            HoldSequence=Terrain->GetAppliedSequence();
            Peers[1]->ClientFieldKeepDigging(true,ReferenceFloor);
            SetFieldStage(EFieldStep::ChargeInterrupted,Now);
        }
        break;
    case EFieldStep::ChargeInterrupted:
        if (Age>.9 && Players[1]->IsHoldingExpeditionDig())
        {
            Peers[1]->ClientShowChart();
            SetFieldStage(EFieldStep::ChargeIdle,Now);
        }
        break;
    case EFieldStep::ChargeIdle:
        if (Age>.8 && LastAction==StepAt && !Players[1]->IsHoldingExpeditionDig())
        {
            Peers[1]->ClientFieldKeepDigging(false,FVector::ZeroVector);
            LastAction=Now;
        }
        if (LastAction>StepAt && Now-LastAction>1.)
        {
            Require(!Players[1]->IsHoldingExpeditionDig() && Terrain->GetAppliedSequence()==HoldSequence,
                TEXT("opening the chart cancels charge; later button release cannot excavate"));
            UE_LOG(LogTemp,Display,TEXT("FIELD_QA_CHARGE_CANCEL reason=chart release_edits=0"));
            TerrainStage=0;
            bPunchTrialSent=bTerrainSampleRequested=false;
            PunchSites={ReferenceFloor,OriginalFloor,SecondFieldTarget};
            SetFieldStage(EFieldStep::PunchTrials,Now);
        }
        break;
    case EFieldStep::PunchTrials:
        if (TickFieldPunchTrials())
        {
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_DONE players=%d assisted=1 protected=1 promoted=1 seized=1 single_seizure=1 cancelled=1 private=1"), Players.Num());
            Advance(EStep::Done);
        }
        break;
    }
}

bool AIslandProbe::TickFieldPunchTrials()
{
    constexpr int32 Round=81;
    constexpr int32 Trials=6;
    const int32 Trial=TerrainStage/2;
    if (Trial>=Trials) return true;
    const bool bAfter=TerrainStage%2!=0;
    if (!bAfter && bPunchTrialSent)
    {
        if (Peers[1]->TriedPunchRound!=Round || Peers[1]->TriedPunchTrial!=Trial) return false;
        bTerrainSampleRequested=bPunchTrialSent=false;
        ++TerrainStage;
        StepAt=FPlatformTime::Seconds();
        return false;
    }
    auto* Terrain=Mode->GetTerrain();
    if (!bTerrainSampleRequested && !bAfter)
    {
        // Use level ground at the same grid alignment for each one-punch profile.
        // A slope changes the vertical depth even for identical surface-normal cuts.
        constexpr double LevelToleranceCm = .5;
        bool bFound=false;
        for (int32 Y=-13; Y<=13 && !bFound; ++Y)
            for (int32 X=-13; X<=13 && !bFound; ++X)
            {
                const FVector Near(350.*X,350.*Y,ReferenceFloor.Z);
                if (PunchSites.ContainsByPredicate([&Near](const FVector& P){ return FVector::DistSquared2D(P,Near)<FMath::Square(335.); })) continue;
                FVector Center,Stand;
                if (!FieldFloor(*Terrain,Near,Center) || !FieldFloor(*Terrain,Near-FVector(112,0,0),Stand)
                    || FMath::Abs(Center.Z-Stand.Z)>LevelToleranceCm || Center.Z<70.) continue;
                bool bClear=true;
                for (int32 R=0; R<=160 && bClear; R+=10)
                {
                    FVector Sample;
                    bClear=FieldFloor(*Terrain,Center+FVector(0,R,0),Sample)
                        && FMath::Abs(Center.Z-Sample.Z)<=LevelToleranceCm;
                }
                if (!bClear) continue;
                const auto* Hero=Mode->GetPlayers()[1]->GetHero();
                const FVector Position=Stand+FVector(0,0,Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.);
                const FCollisionShape Shape=FCollisionShape::MakeCapsule(Hero->GetCapsuleComponent()->GetScaledCapsuleRadius(),Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
                const FCollisionQueryParams Query(SCENE_QUERY_STAT(FieldChargeStanding),false,Hero);
                if (GetWorld()->OverlapBlockingTestByChannel(Position,FQuat::Identity,ECC_Pawn,Shape,Query)) continue;
                FHitResult Aim;
                if (!Terrain->TraceRock(Position+Hero->GetAimOrigin()-Hero->GetActorLocation(),Center-FVector(0,0,1),Hero,Aim)
                    || !Aim.ImpactPoint.Equals(Center,3.)) continue;
                TerrainSamplePoint=Center;
                TerrainInputSequence=Terrain->GetAppliedSequence();
                Place(1,Position);
                if (Trial>=2) PunchSites.Add(Center);
                UE_LOG(LogTemp,Display,TEXT("FIELD_PUNCH_TRIAL trial=%d sequence=%d floor=%s"),Trial,TerrainInputSequence,*Center.ToCompactString());
                bFound=true;
            }
        if (!bFound) { Require(false,TEXT("fresh island charge strip within reach")); return false; }
    }
    const int32 Expected=TerrainInputSequence+int32(bAfter && Trial>=2);
    if (int32(Terrain->GetAppliedSequence())<Expected) return false;
    Require(int32(Terrain->GetAppliedSequence())==Expected,TEXT("main island punch changes exactly its expected edit count"));
    if (!bTerrainSampleRequested)
    {
        for (auto& Peer : Peers)
            Peer->ClientSampleArenaTerrain(Round,TEXT("MainIsland"),10+TerrainStage,TerrainSamplePoint,Expected);
        bTerrainSampleRequested=true;
        return false;
    }
    for (auto& Peer : Peers)
        if (Peer->SampledTerrainRound!=Round || Peer->SampledTerrainStage!=10+TerrainStage) return false;
    if (!bAfter && !bPunchTrialSent)
    {
        Peers[1]->ClientTryArenaPunch(Round,Trial,TerrainSamplePoint);
        bPunchTrialSent=true;
        return false;
    }
    bTerrainSampleRequested=bPunchTrialSent=false;
    ++TerrainStage;
    StepAt=FPlatformTime::Seconds();
    return TerrainStage==2*Trials;
}
