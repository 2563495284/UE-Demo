#pragma once

#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/RotationMatrix.h"

namespace PartyCharacterControl
{
    constexpr float StandingHalfHeight = 90.f;
    constexpr float WalkSpeed = 370.f;

    inline void ConfigureMovement(ACharacter& Character)
    {
        Character.GetCapsuleComponent()->InitCapsuleSize(40.f, StandingHalfHeight);
        Character.bUseControllerRotationYaw = false;
        UCharacterMovementComponent& Movement = *Character.GetCharacterMovement();
        Movement.bOrientRotationToMovement = true;
        Movement.RotationRate = FRotator(0., 620., 0.);
        Movement.MaxWalkSpeed = WalkSpeed;
        Movement.BrakingDecelerationWalking = 1800.f;
        Movement.JumpZVelocity = 445.f;
        Movement.AirControl = .54f;
        Movement.MaxStepHeight = 45.f;
    }

    inline void Move(ACharacter& Character, const FVector2D& Input, double ViewYaw, bool bClimbing = false)
    {
        const FRotationMatrix Basis(FRotator(0., ViewYaw, 0.));
        Character.AddMovementInput(bClimbing ? FVector::UpVector : Basis.GetUnitAxis(EAxis::X), Input.Y);
        Character.AddMovementInput(Basis.GetUnitAxis(EAxis::Y), Input.X);
    }

    inline float GaitSpeed(const ACharacter& Character)
    {
        return FMath::Clamp(float(Character.GetVelocity().Size2D()) / WalkSpeed, 0.f, 1.f);
    }
}
