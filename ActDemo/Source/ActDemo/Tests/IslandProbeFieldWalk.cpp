#include "Tests/IslandProbe.h"

#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Island/IslandPlayerController.h"
#include "Voxel/VoxelCharacter.h"

void AIslandProbePeer::ClientWalkToDig_Implementation(FVector Target)
{
    FieldWalkTarget = Target;
}

void AIslandProbePeer::TickFieldWalk(float DeltaSeconds)
{
    if (!FieldWalkTarget) return;
    auto* Player = CastChecked<AIslandPlayerController>(GetOwner());
    auto* Hero = Player->GetHero();
    if (!Hero) return;
    const FVector Remaining = (*FieldWalkTarget - Hero->GetActorLocation()).GetSafeNormal2D();
    const double Distance = FVector::Dist2D(*FieldWalkTarget, Hero->GetActorLocation());
    const double MaxWalk = FParse::Param(FCommandLine::Get(),TEXT("ExpeditionNeutralQA")) ? 2400. : 750.;
    if (Distance > MaxWalk)
    {
        FieldWalkTarget.Reset();
        return;
    }
    if (Distance < 12.)
    {
        const double Feet = Hero->GetActorLocation().Z - Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if (Hero->GetCharacterMovement()->IsMovingOnGround() && FMath::Abs(Feet - FieldWalkTarget->Z) < 45.
            && Hero->GetVelocity().SizeSquared() < FMath::Square(10.))
        {
            UE_LOG(LogTemp, Display, TEXT("FIELD_QA_WALK_INPUT_END xy_error=%.1f vertical_error=%.1f"), Distance, Feet - FieldWalkTarget->Z);
            FieldWalkTarget.Reset();
        }
        // Release movement to brake. Reacquire the target if momentum carries us across the bowl.
        return;
    }
    const FRotator TargetLook(-23., Remaining.Rotation().Yaw, 0.);
    const FRotator Look = FMath::RInterpConstantTo(Player->GetControlRotation(), TargetLook, DeltaSeconds, 180.);
    Player->SetControlRotation(Look);
    const FRotationMatrix Basis(FRotator(0., Look.Yaw, 0.));
    const FVector2D Stick(FVector::DotProduct(Remaining, Basis.GetUnitAxis(EAxis::Y)),
        FVector::DotProduct(Remaining, Basis.GetUnitAxis(EAxis::X)));
    // Apply the same input as walking forward. CharacterMovement owns gravity, collision
    // and momentum; the server checks where the guest actually comes to rest.
    Player->ApplyMove(Stick * FMath::Clamp(Distance / 110., .22, 1.));
}
