#include "Voxel/VoxelPlayerController.h"

#include "Camera/PlayerCameraManager.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelGameMode.h"
#include "Voxel/VoxelInputConfig.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    constexpr double RecoverHoldSeconds = 2.0;
}

AVoxelPlayerController::AVoxelPlayerController()
{
    static ConstructorHelpers::FObjectFinder<UActInputConfig> Config(TEXT("/Game/ActDemo/Input/DA_Input"));
    InputConfig = Config.Object;
}

AVoxelCharacter* AVoxelPlayerController::GetHero() const
{
    return Cast<AVoxelCharacter>(GetPawn());
}

void AVoxelPlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController())
    {
        return;
    }
    SetInputMode(FInputModeGameOnly());
    bShowMouseCursor = false;
    if (PlayerCameraManager)
    {
        PlayerCameraManager->ViewPitchMin = -82.f;
        PlayerCameraManager->ViewPitchMax = 70.f;
    }
    if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()); Subsystem && InputConfig)
    {
        Subsystem->AddMappingContext(InputConfig->Gameplay, 0);
    }
}

void AVoxelPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
#if !UE_BUILD_SHIPPING
    // Drive the same controller actions from the test probe without desktop mouse input.
    if ((FParse::Param(FCommandLine::Get(), TEXT("VoxelQA")) || FParse::Param(FCommandLine::Get(), TEXT("IslandQA"))))
    {
        return;
    }
#endif
    UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
    if (!Input || !InputConfig)
    {
        return;
    }
    Input->BindAction(InputConfig->Move, ETriggerEvent::Triggered, this, &AVoxelPlayerController::Move);
    Input->BindAction(InputConfig->Look, ETriggerEvent::Triggered, this, &AVoxelPlayerController::Look);
    Input->BindAction(InputConfig->Dodge, ETriggerEvent::Started, this, &AVoxelPlayerController::Jump);
    Input->BindAction(InputConfig->Light, ETriggerEvent::Started, this, &AVoxelPlayerController::Punch);
    Input->BindAction(InputConfig->Heavy, ETriggerEvent::Started, this, &AVoxelPlayerController::DigDown);
    Input->BindAction(InputConfig->Heal, ETriggerEvent::Started, this, &AVoxelPlayerController::Fill);
    Input->BindAction(InputConfig->Sprint, ETriggerEvent::Started, this, &AVoxelPlayerController::StartClimb);
    Input->BindAction(InputConfig->Sprint, ETriggerEvent::Completed, this, &AVoxelPlayerController::StopClimb);
    Input->BindAction(InputConfig->Sprint, ETriggerEvent::Canceled, this, &AVoxelPlayerController::StopClimb);
    Input->BindAction(InputConfig->Restart, ETriggerEvent::Started, this, &AVoxelPlayerController::StartRecover);
    Input->BindAction(InputConfig->Restart, ETriggerEvent::Completed, this, &AVoxelPlayerController::StopRecover);
    Input->BindAction(InputConfig->Restart, ETriggerEvent::Canceled, this, &AVoxelPlayerController::StopRecover);
}

void AVoxelPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (!IsLocalController())
    {
        return;
    }
    if (!Terrain.IsValid())
    {
        Terrain = AVoxelTerrain::Find(GetWorld());
    }
    if (!bSentReady && Terrain.IsValid() && Terrain->IsReady() && GetHero())
    {
        bSentReady = true;
        ServerTerrainReady(Terrain->GetChecksum());
    }
}

void AVoxelPlayerController::Move(const FInputActionValue& Value)
{
    ApplyMove(Value.Get<FVector2D>());
}

void AVoxelPlayerController::ApplyMove(const FVector2D& Input)
{
    if (AVoxelCharacter* Hero = GetHero(); Hero && bPlaying)
    {
        Hero->MoveOnGrid(Input, GetControlRotation().Yaw);
    }
}

void AVoxelPlayerController::Look(const FInputActionValue& Value)
{
    const FVector2D Input = Value.Get<FVector2D>();
    AddYawInput(FMath::Clamp(Input.X, -30.0, 30.0) * .18f);
    AddPitchInput(-FMath::Clamp(Input.Y, -30.0, 30.0) * .18f);
}

void AVoxelPlayerController::Jump()
{
    if (AVoxelCharacter* Hero = GetHero(); Hero && bPlaying)
    {
        Hero->Jump();
    }
}

void AVoxelPlayerController::Punch() { SubmitEdit(EVoxelAction::Punch); }
void AVoxelPlayerController::DigDown() { SubmitEdit(EVoxelAction::DigDown); }
void AVoxelPlayerController::Fill() { SubmitEdit(EVoxelAction::Fill); }

void AVoxelPlayerController::SubmitEdit(EVoxelAction Action)
{
    const AVoxelCharacter* Hero = GetHero();
    if (!Hero || !bPlaying)
    {
        return;
    }
    FVector Aim = GetControlRotation().Vector();
    if (Action != EVoxelAction::DigDown)
    {
        FVector CameraLocation;
        FRotator CameraRotation;
        GetPlayerViewPoint(CameraLocation, CameraRotation);
        const FVector End = CameraLocation + CameraRotation.Vector() * 1000.0;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(VoxelCrosshair), false, Hero);
        FHitResult Hit;
        const FVector Target = GetWorld()->LineTraceSingleByChannel(Hit, CameraLocation, End, ECC_Visibility, Params) ? Hit.ImpactPoint : End;
        // The shoulder camera picks the target. The server traces from the character to it.
        Aim = (Target - Hero->GetAimOrigin()).GetSafeNormal();
    }
    SubmitEdit(Action, Aim);
}

void AVoxelPlayerController::SubmitEdit(EVoxelAction Action, const FVector& Aim)
{
    if (AVoxelCharacter* Hero = GetHero(); Hero && bPlaying)
    {
        Hero->PlayTerrainSwing(Action);
        ServerRequestEdit(Action, Aim);
    }
}

void AVoxelPlayerController::StartClimb() { if (bPlaying) { ServerSetClimb(true); } }
void AVoxelPlayerController::StopClimb() { ServerSetClimb(false); }

void AVoxelPlayerController::StartRecover()
{
    if (GetHero() && RecoverStartedAt < 0.0)
    {
        RecoverStartedAt = FPlatformTime::Seconds();
        ServerSetRecover(true);
    }
}

void AVoxelPlayerController::StopRecover()
{
    if (RecoverStartedAt >= 0.0)
    {
        RecoverStartedAt = -1.0;
        ServerSetRecover(false);
    }
}

bool AVoxelPlayerController::IsNoticeVisible() const
{
    return !Notice.IsEmpty() && (!bPlaying || FPlatformTime::Seconds() < NoticeUntil);
}

double AVoxelPlayerController::GetRecoverProgress() const
{
    return RecoverStartedAt < 0.0 ? 0.0 : FMath::Clamp((FPlatformTime::Seconds() - RecoverStartedAt) / RecoverHoldSeconds, 0.0, 1.0);
}

void AVoxelPlayerController::VoxelStatus()
{
    if (Terrain.IsValid() && Terrain->IsReady())
    {
        UE_LOG(LogTemp, Display, TEXT("VOXEL_STATUS side=%s seq=%u crc=%08x chunks=%s"),
            HasAuthority() ? TEXT("host") : TEXT("client"), Terrain->GetAppliedSequence(), Terrain->GetChecksum(), *Terrain->GetChunkChecksums());
    }
}

void AVoxelPlayerController::ServerTerrainReady_Implementation(uint32 Checksum)
{
    if (AVoxelGameMode* Mode = GetWorld()->GetAuthGameMode<AVoxelGameMode>())
    {
        Mode->ReceiveReady(*this, Checksum);
    }
}

void AVoxelPlayerController::ServerRequestEdit_Implementation(EVoxelAction Action, FVector_NetQuantizeNormal Aim)
{
    if (AVoxelGameMode* Mode = GetWorld()->GetAuthGameMode<AVoxelGameMode>())
    {
        Mode->RequestEdit(*this, Action, Aim);
    }
}

void AVoxelPlayerController::ServerSetClimb_Implementation(bool bEnabled)
{
    const AVoxelGameMode* Mode = GetWorld()->GetAuthGameMode<AVoxelGameMode>();
    if (AVoxelCharacter* Hero = GetHero(); Hero && (!bEnabled || (Mode && Mode->CanPlayerAct(*this))))
    {
        Hero->SetWantsClimb(bEnabled);
    }
}

void AVoxelPlayerController::ServerSetRecover_Implementation(bool bEnabled)
{
    FTimerManager& Timers = GetWorldTimerManager();
    if (!bEnabled)
    {
        Timers.ClearTimer(RecoverTimer);
    }
    else if (GetHero() && !Timers.IsTimerActive(RecoverTimer))
    {
        Timers.SetTimer(RecoverTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            if (AVoxelGameMode* Mode = GetWorld()->GetAuthGameMode<AVoxelGameMode>())
            {
                Mode->RecoverPlayer(*this);
            }
        }), RecoverHoldSeconds, false);
    }
}

void AVoxelPlayerController::ClientResetView_Implementation(FRotator View)
{
    SetControlRotation(View);
    RecoverStartedAt = -1.0;
}

void AVoxelPlayerController::ClientSetRoundActive_Implementation(bool bActive, const FString& Message)
{
    bPlaying = bActive;
    Notice = Message;
}

void AVoxelPlayerController::ClientEditRejected_Implementation(const FString& Message)
{
    Notice = Message;
    NoticeUntil = FPlatformTime::Seconds() + 1.25;
#if !UE_BUILD_SHIPPING
    if ((FParse::Param(FCommandLine::Get(), TEXT("VoxelQA")) || FParse::Param(FCommandLine::Get(), TEXT("IslandQA"))))
    {
        UE_LOG(LogTemp, Display, TEXT("VOXEL_REJECT %s"), *Message);
    }
#endif
}

void AVoxelPlayerController::ClientReceiveEdit_Implementation(const FVoxelEdit& Edit)
{
    if (HasAuthority())
    {
        return;
    }
    if (!bPlaying || Edit.Sequence != LastReceivedSequence + 1 || !Terrain.IsValid() || !Terrain->IsValidEdit(Edit))
    {
        TerrainProtocolError();
        return;
    }
    LastReceivedSequence = Edit.Sequence;
    Incoming.Add({Edit, FPlatformTime::Seconds()});
    ApplyNextEdit();
}

void AVoxelPlayerController::ApplyNextEdit()
{
    if (Incoming.IsEmpty() || !Terrain.IsValid() || Terrain->IsBusy())
    {
        return;
    }

    const FIncomingEdit Next = Incoming[0];
    Incoming.RemoveAt(0, 1, EAllowShrinking::No);
    const TWeakObjectPtr<AVoxelPlayerController> WeakPlayer(this);
    if (!Terrain->BuildEdit(Next.Edit, {}, [WeakPlayer, Next](bool bCommitted)
    {
        if (AVoxelPlayerController* Player = WeakPlayer.Get())
        {
            if (bCommitted)
            {
                UE_LOG(LogTemp, Display, TEXT("VOXEL_SAMPLE side=client seq=%u action=%u ms=%.3f"), Next.Edit.Sequence,
                    static_cast<uint32>(Next.Edit.Action), (FPlatformTime::Seconds() - Next.ReceivedAt) * 1000.0);
                // Reliable RPCs can arrive in a burst after retransmission. Start the next ordered build
                // as soon as the previous collision is committed, without waiting for PlayerTick.
                Player->ApplyNextEdit();
            }
            else
            {
                Player->TerrainProtocolError();
            }
        }
    }))
    {
        TerrainProtocolError();
    }
}

void AVoxelPlayerController::TerrainProtocolError()
{
    Incoming.Reset();
    bPlaying = false;
    Notice = TEXT("Terrain sync failed. Reopen the room with the same build.");
    UE_LOG(LogTemp, Error, TEXT("VOXEL_PROTOCOL_ERROR received=%u"), LastReceivedSequence);
}
