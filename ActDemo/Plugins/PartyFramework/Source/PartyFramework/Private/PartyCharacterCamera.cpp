#include "PartyCharacterCamera.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"

void PartyCharacterCamera::Configure(USpringArmComponent& Boom, UCameraComponent& Shoulder, UCameraComponent& Eye)
{
    UpdateBoom(Boom, 0., true);
    Boom.ProbeSize = 12.f;
    Boom.bUsePawnControlRotation = true;
    Shoulder.FieldOfView = 82.f;

    Eye.SetAutoActivate(false);
    Eye.SetRelativeLocation(FVector(0., 0., EyeHeight()));
    Eye.bUsePawnControlRotation = true;
    Eye.FieldOfView = Shoulder.FieldOfView;
    Eye.bEnableFirstPersonScale = true;
    Eye.FirstPersonScale = .5f;
}

void PartyCharacterCamera::UpdateBoom(USpringArmComponent& Boom, double Pitch, bool bCenterWhenLookingDown, bool bCompact)
{
    const double Shoulder = bCenterWhenLookingDown
        ? FMath::GetMappedRangeValueClamped(FVector2D(-74., -55.), FVector2D(0., 66.), FRotator::NormalizeAxis(Pitch))
        : 66.;
    Boom.TargetArmLength = bCompact ? 4.f : 370.f;
    Boom.TargetOffset.Z = EyeHeight(bCompact);
    Boom.SocketOffset = bCompact ? FVector::ZeroVector : FVector(0., Shoulder, 18.);
}
