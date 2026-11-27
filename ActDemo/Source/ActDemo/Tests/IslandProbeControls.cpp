#include "Tests/IslandProbe.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputKeyEventArgs.h"
#include "Island/IslandPlayerController.h"
#include "PartyVoxelArena.h"
#include "Voxel/VoxelCharacter.h"

bool AIslandProbe::MeasureControlPass(AIslandPlayerController& Player, int32 Pass)
{
    if (ControlPassesDone > Pass) return true;
    auto* Pawn = Cast<ACharacter>(Player.GetPawn());
    if (!Pawn || ControlPassesDone != Pass || !Player.IsLocalController() || Player.GetViewTarget() != Pawn ||
        Player.IsPartyTravelLocked() || Player.bShowMouseCursor || !Player.PlayerCameraManager) return false;
    if (const auto* ArenaPawn = Cast<APartyVoxelPawn>(Pawn))
    {
        if (Pass != 1 || !ArenaPawn->HasLocalPartyInput()) return false;
    }
    else if (Pass == 1 || !Player.IsVoxelInputEnabled()) return false;

    const double Now = GetWorld()->GetTimeSeconds();
    auto& Movement = *Pawn->GetCharacterMovement();
    auto Button = [&Player](FKey Key, bool bDown)
    {
        Player.InputKey(FInputKeyEventArgs::CreateSimulated(Key, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
    };
    if (bReleaseControlViewKey)
    {
        Button(EKeys::V, false);
        bReleaseControlViewKey = false;
    }
    auto PerspectiveReady = [&](bool bFirst)
    {
        // Allow the normal V-key handler, pawn and camera manager to complete their ticks.
        if (Now - ControlViewInputAt < .2) return false;
        const auto* ArenaPawn = Cast<APartyVoxelPawn>(Pawn);
        const bool bPawnFirst = ArenaPawn ? ArenaPawn->IsInFirstPersonView() : Player.GetHero()->IsInFirstPersonView();
        if (Player.WantsFirstPersonView() == bFirst) return bPawnFirst == bFirst;
        Button(EKeys::V, true);
        bReleaseControlViewKey = true;
        ControlViewInputAt = Now;
        return false;
    };
    auto Mouse = [&Player](float Direction)
    {
        Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX, IE_Axis, 160.f * Direction, 1));
        Player.InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseY, IE_Axis, -120.f * Direction, 1));
    };
    auto Next = [&](EControlStep NextControlStep)
    {
        ControlStep = NextControlStep;
        ControlStepAt = Now;
    };
    const double Age = Now - ControlStepAt;
    switch (ControlStep)
    {
    case EControlStep::SetUp:
    {
        if (!Movement.IsMovingOnGround()) return false;
        bControlOriginalFirst = Player.WantsFirstPersonView();
        ControlOriginalPose = Pawn->GetActorTransform();
        ControlOriginalView = Player.GetControlRotation();
        // A level unobstructed floor isolates the movement and shoulder camera from each map's geometry.
        const FVector FloorTop = Pawn->GetActorLocation() + FVector(0., 0., 1800.);
        ControlPlatform = NewObject<UBoxComponent>(this);
        ControlPlatform->InitBoxExtent(FVector(900.,900.,10.));
        ControlPlatform->SetCollisionProfileName(TEXT("BlockAll"));
        ControlPlatform->SetWorldLocation(FloorTop - FVector(0.,0.,10.));
        ControlPlatform->RegisterComponent();
        Pawn->TeleportTo(FloorTop + FVector(0., 0., Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3.),
            FRotator::ZeroRotator, false, true);
        Movement.StopMovementImmediately();
        Movement.SetMovementMode(MOVE_Falling);
        Player.SetControlRotation(FRotator::ZeroRotator);
        Next(EControlStep::Settle);
        return false;
    }
    case EControlStep::Settle:
        if (Age < .6 || !Movement.IsMovingOnGround() || !PerspectiveReady(false)) return false;
        ControlMouseStart = Player.GetControlRotation().GetNormalized();
        UE_LOG(LogTemp, Display, TEXT("ARENA_CONTROL_MOUSE pass=%d stage=start yaw=%.3f pitch=%.3f"), Pass,
            ControlMouseStart.Yaw, ControlMouseStart.Pitch);
        Mouse(1.f);
        Next(EControlStep::MouseForward);
        return false;
    case EControlStep::MouseForward:
        if (Age < .25) return false;
        ControlMouseDelta = (Player.GetControlRotation() - ControlMouseStart).GetNormalized();
        UE_LOG(LogTemp, Display, TEXT("ARENA_CONTROL_MOUSE pass=%d stage=forward yaw=%.3f pitch=%.3f"), Pass,
            ControlMouseDelta.Yaw, ControlMouseDelta.Pitch);
        Require(FMath::Abs(ControlMouseDelta.Yaw) > .1 && FMath::Abs(ControlMouseDelta.Pitch) > .1 &&
            FMath::Abs(FRotator::NormalizeAxis(Pawn->GetActorRotation().Yaw)) < .1,
            TEXT("two mouse axes rotate the camera while the resting body keeps its heading"));
        ControlMouseStart = Player.GetControlRotation();
        Mouse(-1.f);
        Next(EControlStep::MouseBack);
        return false;
    case EControlStep::MouseBack:
        if (Age < .25) return false;
        ControlReverseDelta = (Player.GetControlRotation() - ControlMouseStart).GetNormalized();
        UE_LOG(LogTemp, Display, TEXT("ARENA_CONTROL_MOUSE pass=%d stage=reverse yaw=%.3f pitch=%.3f"), Pass,
            ControlReverseDelta.Yaw, ControlReverseDelta.Pitch);
        Require(ControlReverseDelta.Yaw * ControlMouseDelta.Yaw < 0. && ControlReverseDelta.Pitch * ControlMouseDelta.Pitch < 0.,
            TEXT("reversing the mouse reverses both camera axes"));
        Player.SetControlRotation(FRotator::ZeroRotator);
        Next(EControlStep::AlignForWalk);
        return false;
    case EControlStep::AlignForWalk:
        if (Age < .2) return false;
        ControlCameraOffset = Player.PlayerCameraManager->GetCameraLocation() - Pawn->GetActorLocation();
        ControlCameraFOV = Player.PlayerCameraManager->GetFOVAngle();
        Player.SetControlRotation(FRotator::ZeroRotator);
        Button(EKeys::D, true);
        Next(EControlStep::SideStep);
        return false;
    case EControlStep::SideStep:
        if (Age < .55) return false;
        ControlSideSpeed = Pawn->GetVelocity().Y;
        ControlBodyTurn = Pawn->GetActorRotation().Yaw;
        ControlJumpBase = Pawn->GetActorLocation().Z;
        ControlJumpHeight = 0.;
        Button(EKeys::SpaceBar, true);
        Next(EControlStep::Jump);
        return false;
    case EControlStep::Jump:
        ControlJumpHeight = FMath::Max(ControlJumpHeight, Pawn->GetActorLocation().Z - ControlJumpBase);
        if (Age < .25 || !Movement.IsMovingOnGround()) return false;
        Button(EKeys::SpaceBar, false);
        Button(EKeys::D, false);
        Next(EControlStep::EyeView);
        return false;
    case EControlStep::EyeView:
        if (!PerspectiveReady(true)) return false;
        ControlEyeOffset = Player.PlayerCameraManager->GetCameraLocation() - Pawn->GetActorLocation();
        ControlEyeFOV = Player.PlayerCameraManager->GetFOVAngle();
        Next(EControlStep::Restore);
        return false;
    case EControlStep::Restore:
        if (!PerspectiveReady(bControlOriginalFirst)) return false;
        UE_LOG(LogTemp, Display, TEXT("ARENA_CONTROL_SAMPLE pass=%d yaw=%.3f pitch=%.3f reverse_yaw=%.3f reverse_pitch=%.3f side_cm_s=%.2f body_yaw=%.2f jump_cm=%.2f camera_x=%.2f camera_y=%.2f camera_z=%.2f fov=%.2f eye_x=%.2f eye_y=%.2f eye_z=%.2f eye_fov=%.2f restored_first=%d"),
            Pass, ControlMouseDelta.Yaw, ControlMouseDelta.Pitch, ControlReverseDelta.Yaw, ControlReverseDelta.Pitch,
            ControlSideSpeed, ControlBodyTurn, ControlJumpHeight,
            ControlCameraOffset.X, ControlCameraOffset.Y, ControlCameraOffset.Z, ControlCameraFOV,
            ControlEyeOffset.X, ControlEyeOffset.Y, ControlEyeOffset.Z, ControlEyeFOV, Player.WantsFirstPersonView());
        Pawn->TeleportTo(ControlOriginalPose.GetLocation(), ControlOriginalPose.Rotator(), false, true);
        Movement.StopMovementImmediately();
        Movement.SetMovementMode(MOVE_Falling);
        Player.SetControlRotation(ControlOriginalView);
        ControlPlatform->DestroyComponent();
        ControlPlatform = nullptr;
        ++ControlPassesDone;
        Next(EControlStep::SetUp);
        return true;
    }
    return false;
}
