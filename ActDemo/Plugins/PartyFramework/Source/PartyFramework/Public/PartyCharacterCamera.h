#pragma once

#include "CoreTypes.h"

class UCameraComponent;
class USpringArmComponent;

namespace PartyCharacterCamera
{
    constexpr float EyeHeight(bool bCompact = false) { return bCompact ? 10.f : 49.f; }

    PARTYFRAMEWORK_API void Configure(USpringArmComponent& Boom, UCameraComponent& Shoulder, UCameraComponent& Eye);
    PARTYFRAMEWORK_API void UpdateBoom(USpringArmComponent& Boom, double Pitch, bool bCenterWhenLookingDown, bool bCompact = false);
}
