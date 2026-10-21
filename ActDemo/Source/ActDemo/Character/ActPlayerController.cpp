#include "Character/ActPlayerController.h"
#include "Character/ActCharacter.h"
#include "Combat/ActCombatTypes.h"
#include "UI/ActHUDWidget.h"
#include "World/ActGameMode.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"

AActPlayerController::AActPlayerController() {}
AActCharacter* AActPlayerController::Hero() const { return Cast<AActCharacter>(GetPawn()); }
void AActPlayerController::BeginPlay()
{
 Super::BeginPlay();
 if (!InputConfig) InputConfig = LoadObject<UActInputConfig>(nullptr, TEXT("/Game/ActDemo/Input/DA_Input.DA_Input"));
 UClass* HUDClass = LoadClass<UActHUDWidget>(nullptr, TEXT("/Game/ActDemo/UI/WBP_HUD.WBP_HUD_C"));
 HUD = CreateWidget<UActHUDWidget>(this, HUDClass ? HUDClass : UActHUDWidget::StaticClass());
 HUD->AddToViewport(); HUD->Observe(Hero());
 UpdateInputMode(false);
 PlayerCameraManager->ViewPitchMin=-55.f;
 PlayerCameraManager->ViewPitchMax=30.f;
 SetControlRotation(FRotator(-15,0,0));
}
void AActPlayerController::OnPossess(APawn* Pawn)
{
 Super::OnPossess(Pawn);
 if (HUD) HUD->Observe(Hero());
}
void AActPlayerController::SetupInputComponent()
{
 Super::SetupInputComponent();
 if (!InputConfig) InputConfig = LoadObject<UActInputConfig>(nullptr, TEXT("/Game/ActDemo/Input/DA_Input.DA_Input"));
 auto* Input = Cast<UEnhancedInputComponent>(InputComponent);
 if (!InputConfig || !Input) return;
 Input->BindAction(InputConfig->Move, ETriggerEvent::Triggered, this, &AActPlayerController::Move);
 Input->BindAction(InputConfig->Move, ETriggerEvent::Completed, this, &AActPlayerController::StopMove);
 Input->BindAction(InputConfig->Look, ETriggerEvent::Triggered, this, &AActPlayerController::Look);
 Input->BindAction(InputConfig->Light, ETriggerEvent::Started, this, &AActPlayerController::Light);
 Input->BindAction(InputConfig->Heavy, ETriggerEvent::Started, this, &AActPlayerController::Heavy);
 Input->BindAction(InputConfig->Dodge, ETriggerEvent::Started, this, &AActPlayerController::Dodge);
 Input->BindAction(InputConfig->Heal, ETriggerEvent::Started, this, &AActPlayerController::Heal);
 Input->BindAction(InputConfig->Lock, ETriggerEvent::Started, this, &AActPlayerController::Lock);
 Input->BindAction(InputConfig->Switch, ETriggerEvent::Started, this, &AActPlayerController::Switch);
 Input->BindAction(InputConfig->Sprint, ETriggerEvent::Started, this, &AActPlayerController::SprintStart);
 Input->BindAction(InputConfig->Sprint, ETriggerEvent::Completed, this, &AActPlayerController::SprintStop);
 Input->BindAction(InputConfig->Pause, ETriggerEvent::Started, this, &AActPlayerController::TogglePause);
 Input->BindAction(InputConfig->Restart, ETriggerEvent::Started, this, &AActPlayerController::Retry);
 Input->BindAction(InputConfig->Debug, ETriggerEvent::Started, this, &AActPlayerController::Debug);
}
void AActPlayerController::UpdateInputMode(bool bMenu)
{
 FlushPressedKeys();
 if (Hero()) Hero()->ClearInput();
 if (auto* System = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
 {
  if (InputConfig)
  {
   System->RemoveMappingContext(InputConfig->Gameplay);
   System->RemoveMappingContext(InputConfig->Menu);
   System->AddMappingContext(bMenu ? InputConfig->Menu : InputConfig->Gameplay, bMenu ? 100 : 0);
  }
 }
 bShowMouseCursor = bMenu;
 if (bMenu) { FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode); }
 else { FInputModeGameOnly Mode; SetInputMode(Mode); }
}
void AActPlayerController::Move(const FInputActionValue& Value)
{
 auto* Character = Hero(); if (!Character || !Character->IsAlive() || IsPaused()) return;
 const FVector2D Axis = Value.Get<FVector2D>();
 const FRotationMatrix Basis(FRotator(0, GetControlRotation().Yaw, 0));
 Character->DesiredMove = Basis.GetUnitAxis(EAxis::X) * Axis.Y + Basis.GetUnitAxis(EAxis::Y) * Axis.X;
 if (!Character->IsBusy()) Character->AddMovementInput(Character->DesiredMove.GetSafeNormal(), FMath::Min(1.f, Axis.Length()));
}
void AActPlayerController::StopMove() { if (Hero()) Hero()->DesiredMove = FVector::ZeroVector; }
void AActPlayerController::Look(const FInputActionValue& Value)
{ if (!IsPaused() && Hero() && Hero()->IsAlive() && !Hero()->LockTarget) { auto V = Value.Get<FVector2D>(); AddYawInput(FMath::Clamp(V.X,-30.f,30.f)*.18f); AddPitchInput(-FMath::Clamp(V.Y,-30.f,30.f)*.18f); } }
void AActPlayerController::Light() { if (Hero()) Hero()->RequestAbility(ActTags::Light); }
void AActPlayerController::Heavy() { if (Hero()) Hero()->RequestAbility(ActTags::Heavy); }
void AActPlayerController::Dodge() { if (Hero()) Hero()->RequestAbility(ActTags::Dodge); }
void AActPlayerController::Heal() { if (Hero()) Hero()->RequestAbility(ActTags::Heal); }
void AActPlayerController::Lock() { if (Hero() && !IsPaused()) Hero()->ToggleLock(); }
void AActPlayerController::Switch() { if (Hero() && !IsPaused()) Hero()->SwitchLock(); }
void AActPlayerController::SprintStart() { if (Hero()) Hero()->SetSprint(true); }
void AActPlayerController::SprintStop() { if (Hero()) Hero()->SetSprint(false); }
void AActPlayerController::TogglePause()
{
 auto* Game = GetWorld()->GetAuthGameMode<AActGameMode>();
 if (!Game || Game->Outcome != EActOutcome::Playing) return;
 SetPause(!IsPaused()); UpdateInputMode(IsPaused());
 if (HUD) HUD->Refresh();
}
void AActPlayerController::ShowOutcome() { UpdateInputMode(true); if (HUD) HUD->Refresh(); }
void AActPlayerController::Retry()
{
 if (auto* Game = GetWorld()->GetAuthGameMode<AActGameMode>())
 { if (Game->Outcome == EActOutcome::Playing && !IsPaused()) return; SetPause(false); Game->ResetRun(false); UpdateInputMode(false); }
}
void AActPlayerController::RestartRun()
{ if (auto* Game = GetWorld()->GetAuthGameMode<AActGameMode>()) { SetPause(false); Game->ResetRun(true); UpdateInputMode(false); } }
void AActPlayerController::Debug() { for (TActorIterator<AActCharacter> It(GetWorld()); It; ++It) It->bDebugCombat = !It->bDebugCombat; }
