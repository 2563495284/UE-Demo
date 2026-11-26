#include "Voxel/VoxelGameMode.h"

#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Island/IslandBreakable.h"
#include "Island/IslandState.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/VoxelPlaygroundProbe.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelHUD.h"
#include "Voxel/VoxelPlayerController.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    constexpr double EditCooldown = .5;
}

AVoxelGameMode::AVoxelGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = AVoxelCharacter::StaticClass();
    PlayerControllerClass = AVoxelPlayerController::StaticClass();
    HUDClass = AVoxelHUD::StaticClass();
}

void AVoxelGameMode::BeginPlay()
{
    Super::BeginPlay();
    Terrain = AVoxelTerrain::Find(GetWorld());
    ensureMsgf(Terrain, TEXT("Voxel playground map requires a voxel terrain actor"));
#if !UE_BUILD_SHIPPING
    if (GetClass() == StaticClass() && FParse::Param(FCommandLine::Get(), TEXT("VoxelQA")))
    {
        GetWorld()->SpawnActor<AVoxelPlaygroundProbe>();
    }
#endif
}

bool AVoxelGameMode::RoomFull() const
{
    return bLocked || IsAdmissionLocked() || Players.Num() >= RoomCapacity();
}

void AVoxelGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
    if (ErrorMessage.IsEmpty() && RoomFull())
    {
        ErrorMessage = AdmissionError();
    }
}

APlayerController* AVoxelGameMode::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal,
    const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    // Recheck after PreLogin because two connections can begin the handshake together.
    if (RoomFull())
    {
        ErrorMessage = AdmissionError();
        return nullptr;
    }
    return Super::Login(NewPlayer, InRemoteRole, Portal, Options, UniqueId, ErrorMessage);
}

void AVoxelGameMode::PostLogin(APlayerController* NewPlayer)
{
    if (AVoxelPlayerController* Player = Cast<AVoxelPlayerController>(NewPlayer))
    {
        Players.Add(Player);
    }
    Super::PostLogin(NewPlayer);
}

void AVoxelGameMode::Logout(AController* Exiting)
{
    if (Players.Remove(Cast<AVoxelPlayerController>(Exiting)) > 0 && bLocked && StopOnGuestExit())
    {
        bPlaying = false;
        CancelPendingEdits();
        for (AVoxelPlayerController* Player : Players)
        {
            Player->ClientSetRoundActive(false, TEXT("Player left. Reopen the map to play again."));
        }
    }
    Super::Logout(Exiting);
}

AActor* AVoxelGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
    const int32 ExistingSeat = Players.IndexOfByKey(Cast<AVoxelPlayerController>(Player));
    const int32 Seat = ExistingSeat == INDEX_NONE ? Players.Num() : ExistingSeat;
    const FName StartTag(Seat <= 0 ? TEXT("Host") : TEXT("Guest"));
    for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
    {
        if (It->PlayerStartTag == StartTag)
        {
            if (AVoxelPlayerController* VoxelPlayer = Cast<AVoxelPlayerController>(Player))
            {
                VoxelPlayer->StartTransform = It->GetActorTransform();
            }
            return *It;
        }
    }
    return Super::ChoosePlayerStart_Implementation(Player);
}

void AVoxelGameMode::ReceiveReady(AVoxelPlayerController& Player, uint32 Checksum)
{
    if (!Players.Contains(&Player) || bLocked)
    {
        return;
    }
    Player.InitialTerrainChecksum = Checksum;
}

void AVoxelGameMode::TryStartRound()
{
    if (bLocked || bRoundSetupFailed || !IsStartRequested() || !Terrain || !Terrain->IsReady())
    {
        return;
    }
    const uint32 Checksum = Terrain->GetInitialChecksum();
    for (AVoxelPlayerController* Player : Players)
    {
        if (!Player->InitialTerrainChecksum.IsSet() || !Player->GetHero())
        {
            return;
        }
        if (Player->InitialTerrainChecksum.GetValue() != Checksum)
        {
            UE_LOG(LogTemp, Error, TEXT("VOXEL_INITIAL_MISMATCH player=%s crc=%08x"), *Player->GetName(), Player->InitialTerrainChecksum.GetValue());
            Player->InitialTerrainChecksum.Reset();
            Player->ClientSetRoundActive(false, TEXT("Terrain mismatch. Both players need the same build."));
            return;
        }
    }
    FString Error;
    if (!PrepareRound(Error))
    {
        bRoundSetupFailed = true;
        RoundSetupFailed(Error);
        UE_LOG(LogTemp, Error, TEXT("VOXEL_ROUND_INVALID %s"), *Error);
        for (AVoxelPlayerController* Player : Players)
        {
            Player->ClientSetRoundActive(false, Error);
        }
        return;
    }
    bLocked = true;
    bPlaying = true;
    OnRoundStarted();
    for (AVoxelPlayerController* Player : Players)
    {
        Player->ClientSetRoundActive(true, FString());
    }
    UE_LOG(LogTemp, Display, TEXT("VOXEL_ROUND_READY players=%d crc=%08x"), Players.Num(), Checksum);
}

void AVoxelGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    TryStartRound();
    if (!bPlaying)
    {
        return;
    }

    StartNextEdit();
    UpdateEnvironment();
}

void AVoxelGameMode::UpdateEnvironment()
{
    for (AVoxelPlayerController* Player : Players)
    {
        if (const AVoxelCharacter* Hero = Player->GetHero(); Hero && Hero->GetActorLocation().Z < -640)
        {
            RecoverPlayer(*Player);
        }
    }
}

void AVoxelGameMode::RequestEdit(AVoxelPlayerController& Player, EVoxelAction Action, const FVector& Aim, uint8 Power, bool bProtectChest)
{
    EnqueueEdit(Player, Action, Aim, {},Power,bProtectChest);
}

void AVoxelGameMode::EnqueueEdit(AVoxelPlayerController& Player, EVoxelAction Action, const FVector& Aim, TOptional<FIntVector> ProbeCenter, uint8 Power, bool bProtectChest)
{
    const AVoxelCharacter* Hero = Player.GetHero();
    if (!Hero || !CanPlayerAct(Player) || Action > EVoxelAction::Fill || Aim.ContainsNaN())
    {
        return;
    }
    const double Now = FPlatformTime::Seconds();
    const bool bAbilityPaced = Terrain && Terrain->Layout == EVoxelLayout::Expedition
        && Action == EVoxelAction::Punch && !ProbeCenter.IsSet();
    if ((!bAbilityPaced && Now - Player.LastRequestAt < EditCooldown) || WorkingPlayer.Get() == &Player
        || Requests.ContainsByPredicate([&](const FRequest& Request) { return Request.Player.Get() == &Player; }))
    {
        return;
    }
    Player.LastRequestAt = Now;
    // A sliding capsule can leave the lip of a shaft while another edit is cooking.
    // Use the pose the server observed at input time and recheck reach on resolution.
    const FVector Origin = Action == EVoxelAction::DigDown ? Hero->GetActorLocation() : Hero->GetAimOrigin();
    Requests.Add({&Player, Action, Origin, Aim.GetSafeNormal(), Now, ProbeCenter, {}, {}, Power, bProtectChest});
    StartNextEdit();
}

bool AVoxelGameMode::MakeEnvironmentFill(const TArray<FVector>& Centers, FVoxelEdit& Fill) const
{
    if (!bPlaying || !Terrain || Centers.IsEmpty() || Centers.Num() > FVoxelEdit::MaxFillCenters)
    {
        return false;
    }
    Fill.Action = EVoxelAction::Fill;
    Fill.BrushId = VoxelPlayground::BrushFor(Fill.Action);
    Fill.Center = VoxelPlayground::QuantizeCenter(Centers[0]);
    for (int32 Index = 1; Index < Centers.Num(); ++Index)
    {
        const auto P = VoxelPlayground::QuantizeCenter(Centers[Index]);
        if (P != Fill.Center) Fill.ExtraFillCenters.AddUnique(P);
    }
    return Terrain->IsValidEdit(Fill);
}

bool AVoxelGameMode::CanStartEnvironmentFill(const TArray<FVector>& Centers) const
{
    FVoxelEdit Fill;
    return MakeEnvironmentFill(Centers, Fill) && Terrain->IsReady() && !Terrain->IsBusy() && Requests.IsEmpty()
        && IsFillAreaFree(Fill) && !Terrain->GetDensity()->PrepareEdit(Fill).Samples.IsEmpty();
}

void AVoxelGameMode::EnqueueEnvironmentFill(const TArray<FVector>& Centers, TFunction<void(bool)> Completion)
{
    FVoxelEdit Fill;
    if (!MakeEnvironmentFill(Centers, Fill))
    {
        Completion(false);
        return;
    }
    FRequest Request{};
    Request.EnvironmentFill = MoveTemp(Fill);
    Request.ReceivedAt = FPlatformTime::Seconds();
    Request.Completion = MoveTemp(Completion);
    Requests.Add(MoveTemp(Request));
    StartNextEdit();
}

void AVoxelGameMode::QueueMudLanding(const FVector& Impact, const FVector& Normal, bool bWideFootprint, TFunction<void(bool)> Completion)
{
    FVoxelEdit Edit;
    Edit.Action = EVoxelAction::Fill;
    Edit.BrushId = VoxelPlayground::MudPad;
    Edit.bWideFootprint = bWideFootprint;
    if (Impact.ContainsNaN() || Normal.ContainsNaN() || Normal.IsNearlyZero())
    {
        if (Completion) Completion(false);
        return;
    }
    Edit.Center = VoxelPlayground::QuantizeCenter(Impact);
    Edit.SurfaceNormal = VoxelPlayground::QuantizeNormal(Normal);
    EnqueueMud(MoveTemp(Edit), MoveTemp(Completion));
}

void AVoxelGameMode::QueueBurialLayer(AVoxelCharacter& Victim, const FVector& FootAnchor, int32 Layer, TFunction<void(bool)> Completion)
{
    if (!HasAuthority() || Victim.GetWorld() != GetWorld() || !Players.Contains(Cast<AVoxelPlayerController>(Victim.GetController()))
        || FootAnchor.ContainsNaN() || Layer < 1 || Layer > 3)
    {
        if (Completion) Completion(false);
        return;
    }
    FVoxelEdit Edit;
    Edit.Action = EVoxelAction::Fill;
    Edit.BrushId = VoxelPlayground::FirstBurialLayer + Layer - 1;
    Edit.Center = VoxelPlayground::QuantizeCenter(FootAnchor);
    EnqueueMud(MoveTemp(Edit), MoveTemp(Completion));
}

void AVoxelGameMode::EnqueueMud(FVoxelEdit Edit, TFunction<void(bool)> Completion)
{
    if (!HasAuthority() || !bPlaying || !Terrain || Terrain->Layout != EVoxelLayout::Expedition || !Terrain->IsValidEdit(Edit))
    {
        if (Completion) Completion(false);
        return;
    }
    FRequest Request{};
    Request.EnvironmentFill = MoveTemp(Edit);
    Request.ReceivedAt = FPlatformTime::Seconds();
    Request.Completion = MoveTemp(Completion);
    Requests.Add(MoveTemp(Request));
    StartNextEdit();
}

bool AVoxelGameMode::CheckMudPlacement(const FVoxelEdit& Edit, TMap<AVoxelCharacter*, FVector>* Lifts) const
{
    const FVector Center(Edit.Center);
    if (Edit.BrushId == VoxelPlayground::MudPad)
    {
        // A queued impact can lose its support if another player digs before this edit commits.
        const FVector Normal = FVector(Edit.SurfaceNormal) * .001;
        FHitResult Support;
        if (!Terrain->TraceRock(Center + Normal * 14., Center - Normal * 28., nullptr, Support)) return false;
    }
    return CheckMudOccupants(Edit, Lifts);
}

bool AVoxelGameMode::CheckMudOccupants(const FVoxelEdit& Edit, TMap<AVoxelCharacter*, FVector>* Lifts) const
{
    const FVector Center(Edit.Center);
    const bool bPad = Edit.BrushId == VoxelPlayground::MudPad;
    const FBox Bounds = VoxelPlayground::PartyBrushBounds(Edit).ExpandBy(4.);
    for (const AVoxelPlayerController* Player : Players)
    {
        AVoxelCharacter* Hero = Player->GetHero();
        if (!Hero) continue;
        const UCapsuleComponent* Capsule = Hero->GetCapsuleComponent();
        const double Radius = Capsule->GetScaledCapsuleRadius();
        const double HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
        const FVector Position = Capsule->GetComponentLocation();
        if (!Bounds.Intersect(FBox::BuildAABB(Position, FVector(Radius, Radius, HalfHeight)))) continue;
        const double Segment = HalfHeight - Radius;
        const int32 Steps = FMath::Max(1, FMath::CeilToInt(2. * Segment / 12.));
        bool bOverlap = false;
        for (int32 Step = 0; Step <= Steps; ++Step)
        {
            const FVector Point = Position + FVector::UpVector * FMath::Lerp(-Segment, Segment, double(Step) / Steps);
            if (VoxelPlayground::PartyBrushDistance(Edit, FIntVector(Point)) < Radius + 3.)
            {
                bOverlap = true;
                break;
            }
        }
        if (!bOverlap) continue;
        // Only a pad directly beneath a standing capsule can raise it. Wall impacts cannot shove a bystander.
        const double FootZ = Position.Z - HalfHeight;
        if (!bPad || Edit.SurfaceNormal.Z < 650 || FVector::DistSquared2D(Position, Center) > FMath::Square(57. * Edit.FootprintScale())
            || FootZ < Center.Z - 10. || FootZ > Center.Z + 40.) return false;
        const FVector Destination(Position.X, Position.Y, Center.Z + 42. + HalfHeight);
        FCollisionQueryParams Query(SCENE_QUERY_STAT(MudLift), false, Hero);
        FHitResult Hit;
        const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
        if (GetWorld()->SweepSingleByChannel(Hit, Position, Destination, FQuat::Identity, ECC_Pawn, Shape, Query)
            && (Hit.bStartPenetrating || Hit.ImpactNormal.Z < .7)) return false;
        if (GetWorld()->OverlapBlockingTestByChannel(Destination, FQuat::Identity, ECC_Pawn, Shape, Query)) return false;
        if (Lifts) Lifts->Add(Hero, Destination);
    }
    return true;
}

void AVoxelGameMode::ClipProtectedMud(FVoxelEdit& Edit) const
{
    const FBox Bounds = VoxelPlayground::PartyBrushBounds(Edit).ExpandBy(VoxelPlayground::Spacing);
    TArray<FBox> Protected;
    GetProtectedMudVolumes(Protected);
    for (const FBox& Box : Protected)
    {
        if (!Bounds.Intersect(Box)) continue;
        FVoxelProtectedBox& Clip = Edit.ProtectedBoxes.AddDefaulted_GetRef();
        Clip.Low = FIntVector(Box.Min) - FIntVector(2);
        Clip.High = FIntVector(Box.Max) + FIntVector(2);
    }
}

void AVoxelGameMode::LiftMudOccupants(const TMap<AVoxelCharacter*, FVector>& Lifts)
{
    for (const auto& Lift : Lifts)
    {
        Lift.Key->SetActorLocation(Lift.Value, false, nullptr, ETeleportType::TeleportPhysics);
        Lift.Key->GetCharacterMovement()->bForceNextFloorCheck = true;
        Lift.Key->GetCharacterMovement()->ForceClientAdjustment();
        Lift.Key->ForceNetUpdate();
    }
}

bool AVoxelGameMode::IsFillAreaFree(const FVoxelEdit& Edit) const
{
    if (Edit.BrushId >= VoxelPlayground::MudPad) return CheckMudPlacement(Edit);
    FVoxelEdit Lobe;
    Lobe.Action = EVoxelAction::Fill;
    Lobe.BrushId = Edit.BrushId;
    Lobe.Center = Edit.Center;
    if (!CanFillAt(Lobe) || !CanPlaceAgainstPlayers(Lobe)) return false;
    for (const FIntVector& Center : Edit.ExtraFillCenters)
    {
        Lobe.Center = Center;
        if (!CanFillAt(Lobe) || !CanPlaceAgainstPlayers(Lobe)) return false;
    }
    return true;
}

void AVoxelGameMode::StartNextEdit()
{
    if (bPumpingEdits) return;
    TGuardValue<bool> Guard(bPumpingEdits, true);
    while (bPlaying && !Requests.IsEmpty() && !Terrain->IsBusy())
    {
        StartFrontRequest();
    }
}

bool AVoxelGameMode::TryStrikeProp(AVoxelCharacter& Hero, const FVector& Aim, uint8 PunchPower, bool bProtectChest)
{
    const FVector From = Hero.GetAimOrigin();
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit, From, From + Aim * AimReach, ECC_Visibility,
        FCollisionQueryParams(SCENE_QUERY_STAT(VoxelScenery), false, &Hero))) return false;
    if (auto* Prop=Cast<AIslandBreakable>(Hit.GetActor())) return Prop->Break(Aim);
    return AIslandSceneryDamage::RemoveAuthoredPiece(Hit);
}

void AVoxelGameMode::StartFrontRequest()
{
    const FRequest Request = MoveTemp(Requests[0]);
    Requests.RemoveAt(0, 1, EAllowShrinking::No);
    AVoxelPlayerController* Player = Request.Player.Get();
    AVoxelCharacter* Hero = Player && Players.Contains(Player) ? Player->GetHero() : nullptr;
    if (!Request.EnvironmentFill && (!Hero || !CanPlayerAct(*Player)))
    {
        return;
    }

    if (Hero && !Request.ProbeCenter.IsSet() && Request.Action == EVoxelAction::Punch && TryStrikeProp(*Hero, Request.Aim, Request.PunchPower, Request.bProtectChest))
    {
        return;
    }

    FVoxelEdit Edit;
    if (Request.EnvironmentFill)
    {
        Edit = Request.EnvironmentFill.GetValue();
        Edit.Sequence = Terrain->GetAppliedSequence() + 1;
        if (Edit.BrushId >= VoxelPlayground::MudPad) ClipProtectedMud(Edit);
    }
    else if (!ResolveEdit(Request, *Hero, Edit))
    {
        Player->ClientEditRejected(TEXT("Aim at rock within reach"));
        return;
    }
    const bool bFill = Edit.Action == EVoxelAction::Fill;
    if (Player && bFill && !CanFillAt(Edit))
    {
        Player->ClientEditRejected(TEXT("Cannot fill over a chest"));
        return;
    }
    if (bFill && !IsFillAreaFree(Edit))
    {
        if (Player) Player->ClientEditRejected(TEXT("Someone is in the way"));
        if (Request.Completion) Request.Completion(false);
        return;
    }

    const TWeakObjectPtr<AVoxelGameMode> WeakMode(this);
    WorkingPlayer = Player;
    const bool bStarted = Terrain->BuildEdit(Edit, [WeakMode, Edit, PlayerRef = Request.Player, bEnvironment = Request.EnvironmentFill.IsSet()]()
    {
        const AVoxelGameMode* Mode = WeakMode.Get();
        if (!Mode || !Mode->bPlaying || (!bEnvironment && (!PlayerRef.IsValid() || !Mode->CanPlayerAct(*PlayerRef.Get())))) return false;
        if (Edit.Action != EVoxelAction::Fill) return true;
        if (Edit.BrushId < VoxelPlayground::MudPad) return Mode->IsFillAreaFree(Edit);
        // Validate everyone first, then lift on the game thread immediately before swapping all collision meshes.
        TMap<AVoxelCharacter*, FVector> Lifts;
        if (!Mode->CheckMudPlacement(Edit, &Lifts)) return false;
        LiftMudOccupants(Lifts);
        return true;
    }, [WeakMode, Edit, Request](bool bCommitted)
    {
        AVoxelGameMode* Mode = WeakMode.Get();
        if (!Mode)
        {
            return;
        }
        Mode->WorkingPlayer.Reset();
        if (!bCommitted)
        {
            if (Request.Completion) Request.Completion(false);
            if (AVoxelPlayerController* Sender = Request.Player.Get(); Sender && Mode->bPlaying)
            {
                Sender->ClientEditRejected(TEXT("Someone moved into the fill area"));
            }
            Mode->StartNextEdit();
            return;
        }
        UE_LOG(LogTemp, Display, TEXT("VOXEL_SAMPLE side=host seq=%u action=%u ms=%.3f"), Edit.Sequence,
            static_cast<uint32>(Edit.Action), (FPlatformTime::Seconds() - Request.ReceivedAt) * 1000.0);
        for (AVoxelPlayerController* Peer : Mode->Players)
        {
            Peer->ClientReceiveEdit(Edit);
        }
        Mode->OnTerrainCommitted(Edit, Request.Player.Get());
        if (Request.Completion) Request.Completion(true);
        Mode->StartNextEdit();
    });
    if (!bStarted)
    {
        WorkingPlayer.Reset();
        if (Player) Player->ClientEditRejected(TEXT("No rock changed"));
        if (Request.Completion) Request.Completion(false);
    }
}

bool AVoxelGameMode::ResolveEdit(const FRequest& Request, AVoxelCharacter& Hero, FVoxelEdit& Edit) const
{
    Edit.Sequence = Terrain->GetAppliedSequence() + 1;
    Edit.Action = Request.Action;
    Edit.BrushId = VoxelPlayground::BrushFor(Edit.Action);
    if (Request.ProbeCenter.IsSet())
    {
        Edit.Center = Request.ProbeCenter.GetValue();
        return true;
    }
    const bool bDown = Request.Action == EVoxelAction::DigDown;
    const bool bFill = Request.Action == EVoxelAction::Fill;
    const FVector& Start = Request.Origin;
    const FVector CurrentOrigin = bDown ? Hero.GetActorLocation() : Hero.GetAimOrigin();
    const FVector Direction = bDown ? -FVector::UpVector : Request.Aim;
    FHitResult Rock;
    if (!Terrain->TraceRock(Start, Start + Direction * AimReach, &Hero, Rock)
        || FVector::DistSquared(CurrentOrigin, Rock.ImpactPoint) > FMath::Square(AimReach))
    {
#if !UE_BUILD_SHIPPING
        if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionRepro")))
        {
            UE_LOG(LogTemp, Warning, TEXT("EXPEDITION_QA_REJECTED_RAY origin=%s footZ=%.1f direction=%s actor=%s component=%s impact=%s"),
                *Start.ToCompactString(), Hero.GetActorLocation().Z - Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),
                *Direction.ToCompactString(), *GetNameSafe(Rock.GetActor()), *GetNameSafe(Rock.GetComponent()), *Rock.ImpactPoint.ToCompactString());
        }
#endif
        return false;
    }

    if (Terrain->Layout == EVoxelLayout::Expedition && Request.Action == EVoxelAction::Punch)
    {
        Edit.BrushId = VoxelPlayground::ShallowCut;
        Edit.PunchPower = Request.PunchPower;
        const auto* Round = GetGameState<AIslandGameState>();
        const auto* Member = Hero.GetPlayerState<AIslandPlayerState>();
        Edit.bWideFootprint = Round && Member && Round->HasDecoyToolBoost(Member->Team);
        Edit.SurfaceNormal = VoxelPlayground::QuantizeNormal(Rock.ImpactNormal);
        Edit.Center = VoxelPlayground::QuantizeCenter(Rock.ImpactPoint);
        return true;
    }
    FVector BrushCenter = Rock.ImpactPoint;
    if (bFill)
    {
        BrushCenter += Rock.ImpactNormal * 37.0;
    }
    else if (bDown)
    {
        BrushCenter.Z -= 67.0;
    }
    else
    {
        BrushCenter += Direction * 44.0;
        BrushCenter.Z -= 37.0;
    }
    Edit.Center = VoxelPlayground::QuantizeCenter(BrushCenter);
    return true;
}

bool AVoxelGameMode::CanPlaceAgainstPlayers(const FVoxelEdit& Edit) const
{
    const FVector Center(Edit.Center);
    const double Radius = VoxelPlayground::RadiusFor(Edit.BrushId);
    for (const AVoxelPlayerController* Player : Players)
    {
        const AVoxelCharacter* Hero = Player->GetHero();
        if (!Hero)
        {
            continue;
        }
        const UCapsuleComponent* Capsule = Hero->GetCapsuleComponent();
        const double CapsuleRadius = Capsule->GetScaledCapsuleRadius();
        const double Segment = Capsule->GetScaledCapsuleHalfHeight_WithoutHemisphere();
        const FVector Offset = FVector::UpVector * Segment;
        const FVector Position = Capsule->GetComponentLocation();
        // Include half a cell of clearance to cover the extracted surface around the sphere.
        if (FMath::PointDistToSegmentSquared(Center, Position - Offset, Position + Offset)
            <= FMath::Square(Radius + CapsuleRadius + VoxelPlayground::Spacing * .5))
        {
            return false;
        }
    }
    return true;
}

void AVoxelGameMode::RecoverPlayer(AVoxelPlayerController& Player)
{
    if (AVoxelCharacter* Hero = Player.GetHero())
    {
        FTransform Safe=Player.StartTransform;
        const FVector Origin=Safe.GetLocation();
        auto* Capsule=Hero->GetCapsuleComponent();
        const double Half=Capsule->GetScaledCapsuleHalfHeight();
        const auto Shape=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Half);
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(VoxelReturnFloor), false, Hero);
        const auto Landing=[&](FVector From)
        {
            FHitResult Floor;
            if (!GetWorld()->LineTraceSingleByChannel(Floor,From,From-FVector(0,0,1400),ECC_Visibility,Query)
                || Floor.ImpactNormal.Z < .72 || Cast<APawn>(Floor.GetActor())) return false;
            const FVector Center=Floor.ImpactPoint+FVector(0,0,Half+4.);
            if (GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,Capsule->GetCollisionObjectType(),Shape,Query,
                FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()))) return false;
            Safe.SetLocation(Center);
            return true;
        };
        bool bFound=Landing(Origin);
        for (int32 Radius=150; !bFound && Radius<=2250; Radius+=150)
            for (int32 Spoke=0; !bFound && Spoke<12; ++Spoke)
                bFound=Landing(Origin+FRotator(0,Spoke*30.,0).Vector()*Radius+FVector(0,0,650));
        if (!bFound)
        {
            GetWorld()->SpawnActor<AIslandRescueRaft>(AIslandRescueRaft::StaticClass(),Origin-FVector(0,0,Half+22.),FRotator::ZeroRotator);
        }
        Hero->ReturnToStart(Safe);
        Player.ClientResetView(Safe.Rotator());
    }
}

bool AVoxelGameMode::CanPlayerAct(const AVoxelPlayerController& Player) const
{
    return bPlaying && Player.GetHero() && Players.Contains(&Player);
}

void AVoxelGameMode::FinishRound(const FString& Message)
{
    bPlaying = false;
    CancelPendingEdits();
    for (AVoxelPlayerController* Player : Players)
    {
        Player->ClientSetRoundActive(false, Message);
    }
}

void AVoxelGameMode::CancelPendingEdits()
{
    auto Pending = MoveTemp(Requests);
    for (const auto& Request : Pending)
        if (Request.Completion) Request.Completion(false);
}
