#include "Voxel/VoxelGameMode.h"

#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
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
    if (RoomCapacity() == 2 && FParse::Param(FCommandLine::Get(), TEXT("VoxelQA")))
    {
        GetWorld()->SpawnActor<AVoxelPlaygroundProbe>();
    }
#endif
}

bool AVoxelGameMode::RoomFull() const
{
    return bLocked || Players.Num() >= RoomCapacity();
}

void AVoxelGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
    if (ErrorMessage.IsEmpty() && RoomFull())
    {
        ErrorMessage = TEXT("The voxel room is full or already started.");
    }
}

APlayerController* AVoxelGameMode::Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal,
    const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    // Recheck after PreLogin because two connections can begin the handshake together.
    if (RoomFull())
    {
        ErrorMessage = TEXT("The voxel room is full or already started.");
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
        Requests.Reset();
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
    if (bLocked || bRoundSetupFailed || Players.Num() != RoomCapacity() || !Terrain || !Terrain->IsReady())
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

void AVoxelGameMode::RequestEdit(AVoxelPlayerController& Player, EVoxelAction Action, const FVector& Aim)
{
    EnqueueEdit(Player, Action, Aim, {});
}

void AVoxelGameMode::EnqueueEdit(AVoxelPlayerController& Player, EVoxelAction Action, const FVector& Aim, TOptional<FIntVector> ProbeCenter)
{
    if (!CanPlayerAct(Player) || Action > EVoxelAction::Fill || Aim.ContainsNaN())
    {
        return;
    }
    const double Now = FPlatformTime::Seconds();
    if (Now - Player.LastRequestAt < EditCooldown || WorkingPlayer.Get() == &Player
        || Requests.ContainsByPredicate([&](const FRequest& Request) { return Request.Player.Get() == &Player; }))
    {
        return;
    }
    Player.LastRequestAt = Now;
    Requests.Add({&Player, Action, Aim.GetSafeNormal(), Now, ProbeCenter});
}

void AVoxelGameMode::StartNextEdit()
{
    if (Requests.IsEmpty() || Terrain->IsBusy())
    {
        return;
    }
    const FRequest Request = Requests[0];
    Requests.RemoveAt(0, 1, EAllowShrinking::No);
    AVoxelPlayerController* Player = Request.Player.Get();
    AVoxelCharacter* Hero = Player && Players.Contains(Player) ? Player->GetHero() : nullptr;
    if (!Hero || !CanPlayerAct(*Player))
    {
        return;
    }

    if (!Request.ProbeCenter.IsSet() && Request.Action == EVoxelAction::Punch && TryStrikeProp(*Hero, Request.Aim))
    {
        return;
    }

    FVoxelEdit Edit;
    if (!ResolveEdit(Request, *Hero, Edit))
    {
        Player->ClientEditRejected(TEXT("Aim at rock within reach"));
        return;
    }
    const bool bFill = Edit.Action == EVoxelAction::Fill;
    if (bFill && !CanFillAt(Edit))
    {
        Player->ClientEditRejected(TEXT("Cannot fill over a chest"));
        return;
    }
    if (bFill && !CanPlaceAgainstPlayers(Edit))
    {
        Player->ClientEditRejected(TEXT("Someone is in the way"));
        return;
    }

    const TWeakObjectPtr<AVoxelGameMode> WeakMode(this);
    const bool bStarted = Terrain->BuildEdit(Edit, [WeakMode, Edit, PlayerRef = Request.Player]()
    {
        const AVoxelGameMode* Mode = WeakMode.Get();
        return Mode && PlayerRef.IsValid() && Mode->CanPlayerAct(*PlayerRef.Get())
            && (Edit.Action != EVoxelAction::Fill || (Mode->CanFillAt(Edit) && Mode->CanPlaceAgainstPlayers(Edit)));
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
            if (AVoxelPlayerController* Sender = Request.Player.Get(); Sender && Mode->bPlaying)
            {
                Sender->ClientEditRejected(TEXT("Someone moved into the fill area"));
            }
            return;
        }
        UE_LOG(LogTemp, Display, TEXT("VOXEL_SAMPLE side=host seq=%u action=%u ms=%.3f"), Edit.Sequence,
            static_cast<uint32>(Edit.Action), (FPlatformTime::Seconds() - Request.ReceivedAt) * 1000.0);
        for (AVoxelPlayerController* Peer : Mode->Players)
        {
            Peer->ClientReceiveEdit(Edit);
        }
        Mode->OnTerrainCommitted(Edit);
    });
    if (bStarted)
    {
        WorkingPlayer = Player;
    }
    else
    {
        Player->ClientEditRejected(TEXT("No rock changed"));
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
    const FVector Start = bDown ? Hero.GetActorLocation() : Hero.GetAimOrigin();
    const FVector Direction = bDown ? -FVector::UpVector : Request.Aim;
    FHitResult Rock;
    if (!Terrain->TraceRock(Start, Start + Direction * AimReach, &Hero, Rock))
    {
        return false;
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
        Hero->ReturnToStart(Player.StartTransform);
        Player.ClientResetView(Player.StartTransform.Rotator());
    }
}

bool AVoxelGameMode::CanPlayerAct(const AVoxelPlayerController& Player) const
{
    return bPlaying && Player.GetHero() && Players.Contains(&Player);
}

void AVoxelGameMode::FinishRound(const FString& Message)
{
    bPlaying = false;
    Requests.Reset();
    for (AVoxelPlayerController* Player : Players)
    {
        Player->ClientSetRoundActive(false, Message);
    }
}
