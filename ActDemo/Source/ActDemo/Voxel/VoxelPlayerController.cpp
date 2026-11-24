#include "Voxel/VoxelPlayerController.h"

#include "Camera/PlayerCameraManager.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Island/IslandState.h"
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
    if (auto* Subsystem=ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()); Subsystem && InputConfig && InputConfig->Menu)
    {
        if (bOpen) Subsystem->AddMappingContext(InputConfig->Menu, 10);
        else Subsystem->RemoveMappingContext(InputConfig->Menu);
    }
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
            && !FParse::Param(FCommandLine::Get(), TEXT("ExpeditionNeutralQA"))))
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
    Input->BindAction(InputConfig->Light, ETriggerEvent::Completed, this, &AVoxelPlayerController::StopDig);
    Input->BindAction(InputConfig->Light, ETriggerEvent::Canceled, this, &AVoxelPlayerController::StopDig);
    Input->BindAction(InputConfig->Heavy, ETriggerEvent::Completed, this, &AVoxelPlayerController::StopThrow);
    Input->BindAction(InputConfig->Heavy, ETriggerEvent::Canceled, this, &AVoxelPlayerController::StopThrow);
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
    if (bPlaying && bVoxelInputEnabled && (bDigHeld || bThrowHeld))
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

void AVoxelPlayerController::Look(const FInputActionValue& Value)
{
    if (!bVoxelInputEnabled)
    {
        return;
    }
    const FVector2D Input = Value.Get<FVector2D>();
    AddYawInput(FMath::Clamp(Input.X, -30.0, 30.0) * .18f);
    AddPitchInput(-FMath::Clamp(Input.Y, -30.0, 30.0) * .18f);
}

void AVoxelPlayerController::Jump()
{
    if (AVoxelCharacter* Hero = GetHero(); Hero && bPlaying && bVoxelInputEnabled)
    {
        if (UsesExpeditionAbilities())
        {
            Hero->EnsureExpeditionAbilities();
            Hero->GetAbilitySystemComponent()->TryActivateAbilityByClass(UExpeditionJumpAbility::StaticClass());
        }
        else Hero->Jump();
    }
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
    if (bHeld && (!UsesExpeditionAbilities() || !bPlaying || !bVoxelInputEnabled || !GetHero())) return;
    bDigHeld = bHeld;
    if (bHeld) DispatchExpeditionAction(EExpeditionAction::Dig, true, GetExcavationAim(*GetHero()));
    else ServerReleaseExpeditionAction(EExpeditionAction::Dig);
}

void AVoxelPlayerController::SetExpeditionThrowHeld(bool bHeld)
{
    if (bThrowHeld == bHeld) return;
    if (bHeld && (!UsesExpeditionAbilities() || !bPlaying || !bVoxelInputEnabled || !GetHero())) return;
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
    if (!Hero || !bPlaying || !bVoxelInputEnabled)
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
    if (UsesExpeditionAbilities() && FRotator::NormalizeAxis(CameraRotation.Pitch) < -72.f)
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
    if (FRotator::NormalizeAxis(View.Pitch) < -72.f)
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
    if (bPlaying && bVoxelInputEnabled && UsesExpeditionAbilities() && GetHero())
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
    if (AVoxelCharacter* Hero = GetHero(); Hero && bPlaying && bVoxelInputEnabled)
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

void AVoxelPlayerController::StartClimb() { if (!UsesExpeditionAbilities() && bPlaying && bVoxelInputEnabled) { ServerSetClimb(true); } }
void AVoxelPlayerController::StopClimb() { if (!UsesExpeditionAbilities()) ServerSetClimb(false); }

void AVoxelPlayerController::StartRecover()
{
    if (bPlaying && bVoxelInputEnabled && GetHero() && !GetHero()->IsMudStunned() && RecoverStartedAt < 0.0)
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

const FString& AVoxelPlayerController::GetDisplayNotice() const
{
    static const TMap<FString,FString> Messages{
        {TEXT("Aim at rock within reach"),TEXT("请靠近并瞄准岩面")},
        {TEXT("Cannot fill over a chest"),TEXT("宝箱周围不能回填")},
        {TEXT("Someone is in the way"),TEXT("有玩家站在回填区域")},
        {TEXT("Someone moved into the fill area"),TEXT("有玩家进入回填区域，本次回填已取消")},
        {TEXT("No rock changed"),TEXT("这里没有可改变的岩面")},
        {TEXT("Reach an exposed chest"),TEXT("请靠近已露出且无遮挡的箱子")},
        {TEXT("Collect all four clues to open the prize"),TEXT("集齐四条线索才能打开宝藏")},
        {TEXT("Your crew already has this clue"),TEXT("本队已经取得这条线索")}
    };
    if (UsesExpeditionAbilities())
    {
        static const FString Chest = TEXT("宝箱周围需要留出操作空间");
        static const FString Occupied = TEXT("这里已有玩家，土团未能落稳");
        if (Notice == TEXT("Cannot fill over a chest")) return Chest;
        if (Notice == TEXT("Someone is in the way") || Notice == TEXT("Someone moved into the fill area")) return Occupied;
    }
    const FString* Translation=Messages.Find(Notice);
    return Translation ? *Translation : Notice;
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
