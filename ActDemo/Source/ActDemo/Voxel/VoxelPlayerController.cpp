#include "Voxel/VoxelPlayerController.h"

#include "Island/IslandMessageText.h"
#include "ExpeditionText.h"
#include "Camera/PlayerCameraManager.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Island/IslandState.h"
#include "Island/IslandClientSettings.h"
#include "PartyVoxelArena.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "TimerManager.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelGameMode.h"
#include "Voxel/VoxelInputConfig.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    constexpr double RecoverHoldSeconds = 2.0;
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
    // A front-end HUD can already own focus when the controller receives BeginPlay.
    if (bVoxelInputEnabled)
    {
        SetInputMode(FInputModeGameOnly());
        bShowMouseCursor = false;
    }
    if (PlayerCameraManager)
    {
        PlayerCameraManager->ViewPitchMin = -82.f;
        PlayerCameraManager->ViewPitchMax = 70.f;
    }
    UpdateVoxelMapping();
}

void AVoxelPlayerController::SetVoxelInputEnabled(bool bEnabled)
{
    if (bVoxelInputEnabled == bEnabled)
    {
        return;
    }
    bVoxelInputEnabled = bEnabled;
    if (!bEnabled)
    {
        if (IsLocalController()) ReleaseHeldExpeditionInputs();
        StopRecover();
        if (IsLocalController()) StopClimb();
        GetWorldTimerManager().ClearTimer(RecoverTimer);
    }
    UpdateVoxelMapping();
}

void AVoxelPlayerController::SetIslandUIInput(bool bOpen)
{
    SetVoxelInputEnabled(!bOpen);
    // The island menu receives Escape through Slate and the island controller.
}

void AVoxelPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if (InputConfig)
        if (auto* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
            Subsystem->RemoveMappingContext(InputConfig->Gameplay);
    Super::EndPlay(Reason);
}

void AVoxelPlayerController::UpdateVoxelMapping()
{
    if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()); Subsystem && InputConfig)
    {
        if (bVoxelInputEnabled)
        {
            Subsystem->AddMappingContext(InputConfig->Gameplay, 0);
        }
        else
        {
            Subsystem->RemoveMappingContext(InputConfig->Gameplay);
        }
    }
}

void AVoxelPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
#if !UE_BUILD_SHIPPING
    // Drive the same controller actions from the test probe without desktop mouse input.
    if (FParse::Param(FCommandLine::Get(), TEXT("VoxelQA"))
        || (FParse::Param(FCommandLine::Get(), TEXT("IslandQA")) && !FParse::Param(FCommandLine::Get(), TEXT("IslandPartyQA"))
            && !FParse::Param(FCommandLine::Get(), TEXT("ExpeditionFieldQA"))
            && !FParse::Param(FCommandLine::Get(), TEXT("ExpeditionMudQA"))
            && !FParse::Param(FCommandLine::Get(), TEXT("ExpeditionGamesQA"))
            && !FParse::Param(FCommandLine::Get(), TEXT("ExpeditionNeutralQA"))))
    {
        return;
    }
#endif
    UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
    if (!Input)
    {
        return;
    }
    if (!InputConfig) InputConfig = UVoxelInputConfig::Create(*this);
    UpdateVoxelMapping();
    InputComponent->BindKey(EKeys::V, IE_Pressed, this, &AVoxelPlayerController::SwitchPerspective);
    Input->BindAction(InputConfig->Move, ETriggerEvent::Triggered, this, &AVoxelPlayerController::Move);
    Input->BindAction(InputConfig->Look, ETriggerEvent::Triggered, this, &AVoxelPlayerController::Look);
    Input->BindAction(InputConfig->Jump, ETriggerEvent::Started, this, &AVoxelPlayerController::Jump);
    Input->BindAction(InputConfig->Dig, ETriggerEvent::Started, this, &AVoxelPlayerController::Punch);
    Input->BindAction(InputConfig->SecondaryTool, ETriggerEvent::Started, this, &AVoxelPlayerController::DigDown);
    Input->BindAction(InputConfig->Dig, ETriggerEvent::Completed, this, &AVoxelPlayerController::StopDig);
    Input->BindAction(InputConfig->Dig, ETriggerEvent::Canceled, this, &AVoxelPlayerController::StopDig);
    Input->BindAction(InputConfig->SecondaryTool, ETriggerEvent::Completed, this, &AVoxelPlayerController::StopThrow);
    Input->BindAction(InputConfig->SecondaryTool, ETriggerEvent::Canceled, this, &AVoxelPlayerController::StopThrow);
    Input->BindAction(InputConfig->Fill, ETriggerEvent::Started, this, &AVoxelPlayerController::Fill);
    Input->BindAction(InputConfig->Climb, ETriggerEvent::Started, this, &AVoxelPlayerController::StartClimb);
    Input->BindAction(InputConfig->Climb, ETriggerEvent::Completed, this, &AVoxelPlayerController::StopClimb);
    Input->BindAction(InputConfig->Climb, ETriggerEvent::Canceled, this, &AVoxelPlayerController::StopClimb);
    Input->BindAction(InputConfig->Recall, ETriggerEvent::Started, this, &AVoxelPlayerController::StartRecover);
    Input->BindAction(InputConfig->Recall, ETriggerEvent::Completed, this, &AVoxelPlayerController::StopRecover);
    Input->BindAction(InputConfig->Recall, ETriggerEvent::Canceled, this, &AVoxelPlayerController::StopRecover);
}

void AVoxelPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);
    if (!IsLocalController())
    {
        return;
    }
    if (bPlaying && IsVoxelInputEnabled() && (bDigHeld || bThrowHeld))
        SendHeldExpeditionAim();
    if (!Terrain.IsValid())
    {
        Terrain = AVoxelTerrain::Find(GetWorld());
    }
    if (!bSentReady && Terrain.IsValid() && Terrain->IsReady() && GetHero())
    {
        bSentReady = true;
        ServerTerrainReady(Terrain->GetInitialChecksum());
    }
}

void AVoxelPlayerController::Move(const FInputActionValue& Value)
{
    ApplyMove(Value.Get<FVector2D>());
}

void AVoxelPlayerController::ApplyMove(const FVector2D& Input)
{
    if (AVoxelCharacter* Hero = GetHero(); Hero && bPlaying && bVoxelInputEnabled)
    {
        Hero->MoveOnGrid(Input, GetControlRotation().Yaw);
    }
}

FVector2D AVoxelPlayerController::GetLocalLookScale() const
{
    const auto* Settings = UIslandClientSettings::Get();
    if (!Settings) return FVector2D(1., 1.);
    const float Gain = Settings->GetLookSensitivity();
    return FVector2D(Gain, Settings->IsVerticalLookInverted() ? -Gain : Gain);
}

void AVoxelPlayerController::Look(const FInputActionValue& Value)
{
    // Like movement, mouse look stays active while the island chart is held.
    if (!bVoxelInputEnabled)
    {
        return;
    }
    const FVector2D Input = Value.Get<FVector2D>();
    const FVector2D Scale = GetLocalLookScale();
    AddYawInput(FMath::Clamp(Input.X, -30.0, 30.0) * .18f * Scale.X);
    AddPitchInput(-FMath::Clamp(Input.Y, -30.0, 30.0) * .18f * Scale.Y);
}

void AVoxelPlayerController::Jump()
{
    if (AVoxelCharacter* Hero = GetHero(); Hero && bPlaying && IsVoxelInputEnabled())
    {
        if (UsesExpeditionAbilities())
        {
            Hero->EnsureExpeditionAbilities();
            Hero->GetAbilitySystemComponent()->TryActivateAbilityByClass(UExpeditionJumpAbility::StaticClass());
        }
        else Hero->Jump();
    }
}

void AVoxelPlayerController::SwitchPerspective()
{
    const auto* Round=GetWorld()->GetGameState<AIslandGameState>();
    if (!IsLocalController() || !bPlaying || !Round || !Round->bRoomLobby || !GetPawn()
        || GetViewTarget()!=GetPawn() || bShowMouseCursor || IsLookInputIgnored()) return;
    if (auto* ArenaPawn=Cast<APartyVoxelPawn>(GetPawn()))
    {
        if (Round->Phase!=EIslandPhase::PartyPlaying || !ArenaPawn->HasLocalPartyInput()) return;
        bFirstPersonView = !bFirstPersonView;
        ArenaPawn->SetLocalFirstPersonView(bFirstPersonView);
    }
    else if (GetHero() && Round->Phase==EIslandPhase::Hunting && IsVoxelInputEnabled())
        bFirstPersonView = !bFirstPersonView;
}

void AVoxelPlayerController::Punch()
{
    if (UsesExpeditionAbilities()) SetExpeditionDigHeld(true);
    else SubmitEdit(EVoxelAction::Punch);
}
void AVoxelPlayerController::DigDown()
{
    if (UsesExpeditionAbilities()) SetExpeditionThrowHeld(true);
    else SubmitEdit(EVoxelAction::DigDown);
}
void AVoxelPlayerController::StopDig() { SetExpeditionDigHeld(false); }
void AVoxelPlayerController::StopThrow() { SetExpeditionThrowHeld(false); }

void AVoxelPlayerController::SetExpeditionDigHeld(bool bHeld)
{
    if (bDigHeld == bHeld) return;
    if (bHeld && (!UsesExpeditionAbilities() || !bPlaying || !IsVoxelInputEnabled() || !GetHero())) return;
    bDigHeld = bHeld;
    if (bHeld) DispatchExpeditionAction(EExpeditionAction::Dig, true, GetExcavationAim(*GetHero()));
    else ServerReleaseExpeditionAction(EExpeditionAction::Dig);
}

void AVoxelPlayerController::SetExpeditionThrowHeld(bool bHeld)
{
    if (bThrowHeld == bHeld) return;
    if (bHeld && (!UsesExpeditionAbilities() || !bPlaying || !IsVoxelInputEnabled() || !GetHero())) return;
    bThrowHeld = bHeld;
    if (bHeld) DispatchExpeditionAction(EExpeditionAction::Throw, true, GetExpeditionThrowAim(*GetHero()));
    else ServerReleaseExpeditionAction(EExpeditionAction::Throw);
}

void AVoxelPlayerController::ReleaseHeldExpeditionInputs()
{
    SetExpeditionDigHeld(false);
    SetExpeditionThrowHeld(false);
}

void AVoxelPlayerController::Fill() { if (!UsesExpeditionAbilities()) SubmitEdit(EVoxelAction::Fill); }

void AVoxelPlayerController::SubmitEdit(EVoxelAction Action)
{
    const AVoxelCharacter* Hero = GetHero();
    if (!Hero || !bPlaying || !IsVoxelInputEnabled())
    {
        return;
    }
    SubmitEdit(Action, Action == EVoxelAction::DigDown ? -FVector::UpVector : GetExcavationAim(*Hero));
}

FVector AVoxelPlayerController::GetExcavationAim(const AVoxelCharacter& Hero) const
{
    FVector CameraLocation;
    FRotator CameraRotation;
    GetPlayerViewPoint(CameraLocation, CameraRotation);
    if (UsesExpeditionAbilities() && !Hero.IsInFirstPersonView() && FRotator::NormalizeAxis(CameraRotation.Pitch) < -72.f)
        return (FRotator(0., CameraRotation.Yaw, 0.).Vector() * .07f - FVector::UpVector).GetSafeNormal();
    const FVector End = CameraLocation + CameraRotation.Vector() * 1000.0;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(VoxelCrosshair), false, &Hero);
    FHitResult Hit;
    const FVector Target = GetWorld()->LineTraceSingleByChannel(Hit, CameraLocation, End, ECC_Visibility, Params) ? Hit.ImpactPoint : End;
    return (Target - Hero.GetAimOrigin()).GetSafeNormal();
}

bool AVoxelPlayerController::UsesExpeditionAbilities() const
{
    const auto* State = GetWorld() ? GetWorld()->GetGameState<AIslandGameState>() : nullptr;
    return State && State->bRoomLobby;
}

FVector AVoxelPlayerController::GetExpeditionThrowAim(const AVoxelCharacter& Hero) const
{
    FVector Eye;
    FRotator View;
    GetPlayerViewPoint(Eye, View);
    // Looking straight down in a shoulder camera otherwise hits well off to the side
    // of the player's own capsule. In this limit the target is the ground at the toes.
    if (!Hero.IsInFirstPersonView() && FRotator::NormalizeAxis(View.Pitch) < -72.f)
        return (FRotator(0., View.Yaw, 0.).Vector() * .07f - FVector::UpVector).GetSafeNormal();

    const FVector Far = Eye + View.Vector() * 1400.;
    FHitResult WorldHit;
    const FCollisionQueryParams Params(SCENE_QUERY_STAT(ExpeditionThrowCrosshair), false, &Hero);
    FVector Target = GetWorld()->LineTraceSingleByChannel(WorldHit, Eye, Far, ECC_Visibility, Params) ? WorldHit.ImpactPoint : Far;
    // Pawn visibility deliberately ignores other players so chest traces stay clear.
    // Intersect the actual capsules separately, clipped by the first scene obstruction.
    for (TActorIterator<AVoxelCharacter> It(GetWorld()); It; ++It)
    {
        const auto* Other = *It;
        FHitResult BodyHit;
        if (Other != &Hero && Other->GetCapsuleComponent()->LineTraceComponent(BodyHit, Eye, Target, Params))
            Target = BodyHit.ImpactPoint;
    }
    return (Target - Hero.GetAimOrigin()).GetSafeNormal();
}

FVector AVoxelPlayerController::GetExpeditionActionAim(EExpeditionAction Action) const
{
    return Action == EExpeditionAction::Throw ? ThrowAim : DigAim;
}

void AVoxelPlayerController::DispatchExpeditionAction(EExpeditionAction Action, bool bHeld, FVector Aim)
{
    if (bPlaying && IsVoxelInputEnabled() && UsesExpeditionAbilities() && GetHero())
        ServerExpeditionAction(Action, bHeld, Aim.GetSafeNormal());
}

void AVoxelPlayerController::SubmitExpeditionThrow(const FVector& Aim)
{
    DispatchExpeditionAction(EExpeditionAction::Throw, false, Aim);
}

void AVoxelPlayerController::SendHeldExpeditionAim()
{
    const double Now = GetWorld()->GetTimeSeconds();
    auto* Hero = GetHero();
    if (!Hero || Now < NextAimUpdate) return;
    NextAimUpdate = Now + .05;
    if (bDigHeld) ServerUpdateExpeditionAim(EExpeditionAction::Dig, GetExcavationAim(*Hero));
    if (bThrowHeld) ServerUpdateExpeditionAim(EExpeditionAction::Throw, GetExpeditionThrowAim(*Hero));
}

void AVoxelPlayerController::ServerUpdateExpeditionAim_Implementation(EExpeditionAction Action, FVector_NetQuantizeNormal Aim)
{
    if (Aim.ContainsNaN() || Aim.IsNearlyZero()) return;
    if (Action == EExpeditionAction::Dig && bServerDigHeld) DigAim = Aim.GetSafeNormal();
    if (Action == EExpeditionAction::Throw && bServerThrowHeld) ThrowAim = Aim.GetSafeNormal();
}

void AVoxelPlayerController::ServerExpeditionAction_Implementation(EExpeditionAction Action, bool bHeld, FVector_NetQuantizeNormal Aim)
{
    auto* Hero = GetHero();
    const auto* Round = GetWorld()->GetGameState<AIslandGameState>();
    if (!Hero || !Round || !Round->bRoomLobby || Round->Phase != EIslandPhase::Hunting || Action > EExpeditionAction::Recall) return;
    if (Action == EExpeditionAction::Dig || Action == EExpeditionAction::Throw)
    {
        if (Aim.ContainsNaN() || Aim.IsNearlyZero()) return;
        if (Action == EExpeditionAction::Dig) { DigAim = Aim.GetSafeNormal(); if (bHeld) bServerDigHeld = true; }
        else { ThrowAim = Aim.GetSafeNormal(); if (bHeld) bServerThrowHeld = true; }
    }
    if (Action != EExpeditionAction::Jump) TriggerExpeditionEvent(Action, bHeld);
}

void AVoxelPlayerController::TriggerExpeditionEvent(EExpeditionAction Action, bool bHeld)
{
    auto* Hero = GetHero();
    if (!Hero) return;
    Hero->EnsureExpeditionAbilities();
    FGameplayEventData Data;
    Data.EventTag = ExpeditionAbilities::ActionTag(Action);
    Data.Instigator = Hero;
    Data.Target = Hero;
    Data.EventMagnitude = bHeld ? 1.f : 0.f;
    Hero->GetAbilitySystemComponent()->HandleGameplayEvent(Data.EventTag, &Data);
}

void AVoxelPlayerController::ServerReleaseExpeditionAction_Implementation(EExpeditionAction Action)
{
    if (Action != EExpeditionAction::Dig && Action != EExpeditionAction::Throw && Action != EExpeditionAction::Recall) return;
    if (Action == EExpeditionAction::Dig) bServerDigHeld = false;
    if (Action == EExpeditionAction::Throw) bServerThrowHeld = false;
    if (auto* Hero = GetHero())
    {
        const FGameplayTagContainer Tags(ExpeditionAbilities::ActionTag(Action));
        Hero->GetAbilitySystemComponent()->CancelAbilities(&Tags);
    }
}

void AVoxelPlayerController::OnMudStunChanged(bool bStunned)
{
    if (bStunned) RecoverStartedAt = -1.;
    if (!bStunned && HasAuthority() && UsesExpeditionAbilities())
    {
        if (bServerDigHeld) TriggerExpeditionEvent(EExpeditionAction::Dig, true);
        if (bServerThrowHeld) TriggerExpeditionEvent(EExpeditionAction::Throw, true);
    }
}

void AVoxelPlayerController::SubmitEdit(EVoxelAction Action, const FVector& Aim)
{
    if (AVoxelCharacter* Hero = GetHero(); Hero && bPlaying && IsVoxelInputEnabled())
    {
        if (UsesExpeditionAbilities())
        {
            if (Action == EVoxelAction::Punch) DispatchExpeditionAction(EExpeditionAction::Dig, false, Aim);
            return;
        }
        Hero->PlayTerrainSwing(Action);
        ServerRequestEdit(Action, Aim);
    }
}

void AVoxelPlayerController::StartClimb() { if (!UsesExpeditionAbilities() && bPlaying && IsVoxelInputEnabled()) { ServerSetClimb(true); } }
void AVoxelPlayerController::StopClimb() { if (!UsesExpeditionAbilities()) ServerSetClimb(false); }

void AVoxelPlayerController::StartRecover()
{
    if (bPlaying && IsVoxelInputEnabled() && GetHero() && !GetHero()->IsMudStunned() && RecoverStartedAt < 0.0)
    {
        RecoverStartedAt = FPlatformTime::Seconds();
        if (UsesExpeditionAbilities()) DispatchExpeditionAction(EExpeditionAction::Recall, true);
        else ServerSetRecover(true);
    }
}

void AVoxelPlayerController::StopRecover()
{
    if (RecoverStartedAt >= 0.0)
    {
        RecoverStartedAt = -1.0;
        if (UsesExpeditionAbilities()) ServerReleaseExpeditionAction(EExpeditionAction::Recall);
        else ServerSetRecover(false);
    }
}

FString AVoxelPlayerController::GetDisplayNotice() const
{
    if (UsesExpeditionAbilities())
    {
        if (Notice == TEXT("Cannot fill over a chest"))
            return EXPEDITION_TEXT("宝箱周围需要留出操作空间", "Leave room to open the chest");
        if (Notice == TEXT("Someone is in the way") || Notice == TEXT("Someone moved into the fill area"))
            return EXPEDITION_TEXT("这里已有玩家，土团未能落稳", "A player is blocking the mud");
    }
    return IslandMessageText::Display(Notice);
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
    if (auto* Hero = GetHero()) Hero->EnsureExpeditionAbilities();
    if (AVoxelGameMode* Mode = GetWorld()->GetAuthGameMode<AVoxelGameMode>())
    {
        Mode->ReceiveReady(*this, Checksum);
    }
}

void AVoxelPlayerController::ServerRequestEdit_Implementation(EVoxelAction Action, FVector_NetQuantizeNormal Aim)
{
    if (UsesExpeditionAbilities()) return;
    if (AVoxelGameMode* Mode = GetWorld()->GetAuthGameMode<AVoxelGameMode>())
    {
        Mode->RequestEdit(*this, Action, Aim);
    }
}

void AVoxelPlayerController::ServerSetClimb_Implementation(bool bEnabled)
{
    if (UsesExpeditionAbilities()) return;
    const AVoxelGameMode* Mode = GetWorld()->GetAuthGameMode<AVoxelGameMode>();
    if (AVoxelCharacter* Hero = GetHero(); Hero && (!bEnabled || (Mode && Mode->CanPlayerAct(*this))))
    {
        Hero->SetWantsClimb(bEnabled);
    }
}

void AVoxelPlayerController::ServerSetRecover_Implementation(bool bEnabled)
{
    if (UsesExpeditionAbilities()) return;
    FTimerManager& Timers = GetWorldTimerManager();
    if (!bEnabled)
    {
        Timers.ClearTimer(RecoverTimer);
    }
    else if (const auto* Mode = GetWorld()->GetAuthGameMode<AVoxelGameMode>();
        Mode && Mode->CanPlayerAct(*this) && !Timers.IsTimerActive(RecoverTimer))
    {
        Timers.SetTimer(RecoverTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
        {
            if (auto* ActiveMode = GetWorld()->GetAuthGameMode<AVoxelGameMode>(); ActiveMode && ActiveMode->CanPlayerAct(*this))
            {
                ActiveMode->RecoverPlayer(*this);
            }
        }), RecoverHoldSeconds, false);
    }
}

void AVoxelPlayerController::ClientResetView_Implementation(FRotator View)
{
    ReleaseHeldExpeditionInputs();
    SetControlRotation(View);
    RecoverStartedAt = -1.0;
}

void AVoxelPlayerController::ClientSetRoundActive_Implementation(bool bActive, const FString& Message)
{
    if (!bActive)
    {
        ReleaseHeldExpeditionInputs();
        StopRecover();
    }
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
    if (bApplyingEdits) return;
    TGuardValue<bool> Guard(bApplyingEdits, true);
    while (!Incoming.IsEmpty() && Terrain.IsValid() && !Terrain->IsBusy())
    {
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
}

void AVoxelPlayerController::TerrainProtocolError()
{
    ReleaseHeldExpeditionInputs();
    StopRecover();
    Incoming.Reset();
    bPlaying = false;
    Notice = TEXT("地形同步失败，请使用同一版本重新加入房间");
    UE_LOG(LogTemp, Error, TEXT("VOXEL_PROTOCOL_ERROR received=%u"), LastReceivedSequence);
}
