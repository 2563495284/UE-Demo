#include "Tests/VoxelPlaygroundProbe.h"

#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "UnrealClient.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelGameMode.h"
#include "Voxel/VoxelPlayerController.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    constexpr uint32 GameplayEdits = 4;
    constexpr uint32 FinalEdit = GameplayEdits + 100;
    const FVector TunnelStart(270, -200, 108);
    const FVector TunnelEnd(520, -200, 108);

    bool HasFinalCollision(const AVoxelTerrain& Terrain)
    {
        FHitResult Hit;
        if (Terrain.TraceRock(TunnelStart, TunnelEnd, nullptr, Hit)
            || !Terrain.TraceRock({277, -365, 260}, {277, -365, 40}, nullptr, Hit))
        {
            return false;
        }
        // The path crosses both the X and Y chunk boundaries under the tunnel.
        for (const FVector Point : {FVector(399, -201, 75), FVector(400, -200, 75), FVector(401, -199, 75)})
        {
            if (!Terrain.TraceRock(Point, Point - FVector(0, 0, 260), nullptr, Hit))
            {
                return false;
            }
        }
        return true;
    }
}

AVoxelPlaygroundProbe::AVoxelPlaygroundProbe()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    bAlwaysRelevant = true;
    SetNetUpdateFrequency(20.f);
}

void AVoxelPlaygroundProbe::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AVoxelPlaygroundProbe, PhaseValue);
}

void AVoxelPlaygroundProbe::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (bFinished)
    {
        return;
    }
    const double Now = FPlatformTime::Seconds();
    AVoxelPlayerController* Player = Cast<AVoxelPlayerController>(GetWorld()->GetFirstPlayerController());
    AVoxelTerrain* Terrain = AVoxelTerrain::Find(GetWorld());
    if (Player && Player->IsPlaying() && Player->GetHero() && Terrain && Terrain->IsReady())
    {
        if (HasAuthority())
        {
            TickHost(*Player, *Terrain, Now);
        }
        else
        {
            TickGuest(*Player, *Terrain, Now);
        }
    }
}

void AVoxelPlaygroundProbe::Place(AVoxelPlayerController& Player, const FVector& Position, FRotator View)
{
    Player.GetHero()->ReturnToStart(FTransform(FRotator(0, View.Yaw, 0), Position));
    Player.SetControlRotation(View);
    Player.ClientResetView(View);
}

void AVoxelPlaygroundProbe::Advance(EPhase Next, double Now)
{
    PhaseValue = static_cast<uint8>(Next);
    UE_LOG(LogTemp, Display, TEXT("VOXEL_QA_PHASE phase=%u"), PhaseValue);
    StartedAt = Now;
    bDidPhaseAction = false;
    ForceNetUpdate();
}

void AVoxelPlaygroundProbe::Check(bool bPassed, const TCHAR* Label)
{
    UE_LOG(LogTemp, Display, TEXT("VOXEL_QA_CHECK side=%s passed=%d %s"), HasAuthority() ? TEXT("host") : TEXT("client"), bPassed, Label);
    if (!bPassed)
    {
        bFinished = true;
        FPlatformMisc::RequestExitWithStatus(false, 1);
    }
}

void AVoxelPlaygroundProbe::TickHost(AVoxelPlayerController& Host, AVoxelTerrain& Terrain, double Now)
{
    if (Phase() == EPhase::Waiting)
    {
        for (TActorIterator<AVoxelPlayerController> It(GetWorld()); It; ++It)
        {
            if (*It != &Host && It->GetHero())
            {
                RemotePlayer = *It;
                break;
            }
        }
        if (RemotePlayer)
        {
            SetOwner(RemotePlayer);
            FHitResult Hit;
            Check(Terrain.GetAppliedSequence() == 0 && Terrain.TraceRock(TunnelStart, TunnelEnd, nullptr, Hit), TEXT("pristine wall blocks the tunnel"));
            Place(Host, {160, -200, 94});
            Place(*RemotePlayer, {190, -200, 94});
            Advance(EPhase::Punch, Now);
        }
        return;
    }
    if (!RemotePlayer || !RemotePlayer->GetHero() || Now - StartedAt > (Phase() == EPhase::Stress ? 140.0 : 9.0))
    {
        UE_LOG(LogTemp, Error, TEXT("VOXEL_QA_TIMEOUT phase=%u seq=%u guest=%s host=%s"), PhaseValue, Terrain.GetAppliedSequence(),
            RemotePlayer && RemotePlayer->GetHero() ? *RemotePlayer->GetHero()->GetActorLocation().ToString() : TEXT("missing"),
            *Host.GetHero()->GetActorLocation().ToString());
        Check(false, TEXT("phase timeout"));
        return;
    }
    if (Now - StartedAt < .45)
    {
        return;
    }
    AVoxelCharacter& Guest = *RemotePlayer->GetHero();
    const uint32 Seq = Terrain.GetAppliedSequence();
    const auto SendOnce = [&](EVoxelAction Action)
    {
        if (!bDidPhaseAction)
        {
            Host.SubmitEdit(Action, Host.GetControlRotation().Vector());
            bDidPhaseAction = true;
        }
    };
    switch (Phase())
    {
    case EPhase::Punch:
        SendOnce(EVoxelAction::Punch);
        if (Seq == 1)
        {
            FHitResult Hit;
            Check(!Terrain.TraceRock(TunnelStart, TunnelEnd, nullptr, Hit), TEXT("cross-chunk tunnel has no collision wall"));
            if (!FParse::Param(FCommandLine::Get(), TEXT("nullrhi")))
            {
                FScreenshotRequest::RequestScreenshot(TEXT("VoxelPlaygroundQA"), true, true, false, FIntRect(), true);
            }
            Advance(EPhase::Walk, Now);
        }
        break;
    case EPhase::Walk:
        if (Guest.GetActorLocation().X > 530)
        {
            Check(true, TEXT("guest CharacterMovement crossed host tunnel"));
            Place(Host, {150, -365, 94});
            Place(*RemotePlayer, {268, -365, 94});
            Advance(EPhase::RejectFill, Now);
        }
        break;
    case EPhase::RejectFill:
        SendOnce(EVoxelAction::Fill);
        if (Host.GetNotice() == TEXT("Someone is in the way"))
        {
            Check(Seq == 1 && !Terrain.IsBusy(), TEXT("occupied fill rejects without a sequence"));
            Place(*RemotePlayer, {170, -510, 94});
            Host.SetControlRotation(FRotator(-31, 0, 0));
            Advance(EPhase::BuildStep, Now);
        }
        break;
    case EPhase::BuildStep:
        if (Now - StartedAt < .65)
        {
            break;
        }
        SendOnce(EVoxelAction::Fill);
        if (Seq == 2)
        {
            Place(*RemotePlayer, {277, -365, 260});
            Advance(EPhase::Land, Now);
        }
        break;
    case EPhase::Land:
        if (Guest.GetCharacterMovement()->IsMovingOnGround() && Guest.GetActorLocation().Z > 140)
        {
            if (!bDidPhaseAction)
            {
                Check(true, TEXT("guest capsule landed on host's fill"));
                bDidPhaseAction = true;
            }
            if (Now - StartedAt > 1.4)
            {
                Place(Host, {166, -30, 94});
                Place(*RemotePlayer, {270, -30, 94});
                Advance(EPhase::Climb, Now);
            }
        }
        break;
    case EPhase::Climb:
        if (!bDidPhaseAction && Guest.IsClimbingRock() && Guest.GetActorLocation().Z >= 140)
        {
            Check(true, TEXT("guest climbed wall using movement input"));
            const FVector HitPoint(350, -30, Guest.GetActorLocation().Z + 40.0);
            Host.SetControlRotation((HitPoint - Host.GetHero()->GetAimOrigin()).Rotation());
            SendOnce(EVoxelAction::Punch);
        }
        if (Seq == 3 && Guest.GetCharacterMovement()->IsFalling())
        {
            Check(true, TEXT("guest released when the wall was punched out"));
            Place(*RemotePlayer, {820, -300, 133});
            Advance(EPhase::Dig, Now);
        }
        break;
    case EPhase::Dig:
        if (Seq == GameplayEdits && Guest.GetActorLocation().Z < 70)
        {
            Check(true, TEXT("guest requested a normal downward edit and fell into the shaft"));
            Advance(EPhase::Recover, Now);
        }
        break;
    case EPhase::Recover:
        if (FVector::Dist2D(Guest.GetActorLocation(), RemotePlayer->StartTransform.GetLocation()) < 15)
        {
            Check(true, TEXT("remote held R and returned to guest start"));
            Place(Host, {-205, -175, 100});
            PreviousFrame = Now;
            Advance(EPhase::Stress, Now);
        }
        break;
    case EPhase::Stress:
        if (PreviousFrame > StartedAt)
        {
            FrameTimesMs.Add((Now - PreviousFrame) * 1000.0);
        }
        PreviousFrame = Now;
        if (Seq == FinalEdit)
        {
            FrameTimesMs.Sort();
            const int32 Index = FMath::CeilToInt(FrameTimesMs.Num() * .95) - 1;
            UE_LOG(LogTemp, Display, TEXT("VOXEL_FRAME_P95 ms=%.3f count=%d"), FrameTimesMs[Index], FrameTimesMs.Num());
            Check(HasFinalCollision(Terrain), TEXT("tunnel, tread and chunk seam collision after replay"));
            Host.VoxelStatus();
            UE_LOG(LogTemp, Display, TEXT("VOXEL_QA_DONE side=host edits=100"));
            Advance(EPhase::Done, Now);
            bFinished = true;
        }
        else if ((Seq - GameplayEdits) % 2 == 1 && Now - LastStressRequest >= .55)
        {
            QueueStressEdit(Host, Seq);
            LastStressRequest = Now;
        }
        break;
    default:
        break;
    }
}

void AVoxelPlaygroundProbe::TickGuest(AVoxelPlayerController& Guest, AVoxelTerrain& Terrain, double Now)
{
    AVoxelCharacter& Hero = *Guest.GetHero();
    if (LastGuestPhase != PhaseValue)
    {
        LastGuestPhase = PhaseValue;
        bDidPhaseAction = false;
    }
    switch (Phase())
    {
    case EPhase::Walk:
        if (Terrain.GetAppliedSequence() >= 1 && Hero.GetActorLocation().X < 550)
        {
            Guest.ApplyMove({0, 1});
        }
        break;
    case EPhase::Land:
        if (!bCheckedLand && Terrain.GetAppliedSequence() == 2 && Hero.GetCharacterMovement()->IsMovingOnGround()
            && Hero.GetActorLocation().Z > 140)
        {
            bCheckedLand = true;
            Check(true, TEXT("guest process stands on rebuilt fill collision"));
        }
        break;
    case EPhase::Climb:
        if (!bDidPhaseAction)
        {
            Guest.StartClimb();
            bDidPhaseAction = true;
        }
        if (Hero.IsClimbingRock() && Hero.GetActorLocation().Z < 153)
        {
            Guest.ApplyMove({0, 1});
        }
        break;
    case EPhase::Dig:
        if (!bDidPhaseAction && FVector::Dist2D(Hero.GetActorLocation(), FVector(820, -300, 133)) < 24.0
            && Hero.GetCharacterMovement()->IsMovingOnGround())
        {
            Guest.StopClimb();
            Guest.SubmitEdit(EVoxelAction::DigDown);
            bDidPhaseAction = true;
        }
        break;
    case EPhase::Recover:
        if (!bDidPhaseAction)
        {
            Guest.StartRecover();
            bDidPhaseAction = true;
        }
        break;
    case EPhase::Stress:
    {
        const uint32 Seq = Terrain.GetAppliedSequence();
        // A retry after a dropped-by-cooldown request is keyed to the last applied version.
        const double RequestInterval = Seq == LastRequestedSequence ? 1.0 : .60;
        if (Seq >= GameplayEdits && Seq < FinalEdit && (Seq - GameplayEdits) % 2 == 0
            && Now - LastStressRequest >= RequestInterval)
        {
            LastRequestedSequence = Seq;
            LastStressRequest = Now;
            ServerStressFill(Seq);
        }
        break;
    }
    case EPhase::Done:
        if (Terrain.GetAppliedSequence() == FinalEdit)
        {
            Check(bCheckedLand && HasFinalCollision(Terrain), TEXT("tunnel, tread and chunk seam collision after replay"));
            Guest.VoxelStatus();
            UE_LOG(LogTemp, Display, TEXT("VOXEL_QA_DONE side=client edits=100"));
            bFinished = true;
        }
        break;
    default:
        break;
    }
}

void AVoxelPlaygroundProbe::ServerStressFill_Implementation(uint32 ExpectedSequence)
{
    if (RemotePlayer && (ExpectedSequence - GameplayEdits) % 2 == 0)
    {
        QueueStressEdit(*RemotePlayer, ExpectedSequence);
    }
}

void AVoxelPlaygroundProbe::QueueStressEdit(AVoxelPlayerController& Player, uint32 ExpectedSequence)
{
    AVoxelGameMode* Mode = GetWorld()->GetAuthGameMode<AVoxelGameMode>();
    if (Phase() != EPhase::Stress || !Mode || Mode->GetTerrain()->GetAppliedSequence() != ExpectedSequence
        || ExpectedSequence < GameplayEdits || ExpectedSequence >= FinalEdit)
    {
        return;
    }
    const bool bFill = (ExpectedSequence - GameplayEdits) % 2 == 0;
    // Use fixed brushes for long replay stress. The four earlier edits go through normal aiming.
    Mode->EnqueueEdit(Player, bFill ? EVoxelAction::Fill : EVoxelAction::Punch, FVector::ForwardVector,
        bFill ? FIntVector(395, -200, 43) : FIntVector(395, -200, 105));
}
