#include "Character/ActAnimInstance.h"
#include "GameFramework/Pawn.h"

void UActAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
 Super::NativeUpdateAnimation(DeltaSeconds);
 if (const APawn* Pawn=TryGetPawnOwner())
 {
  const FVector Local=Pawn->GetActorRotation().UnrotateVector(Pawn->GetVelocity());
  Speed=Local.Size2D();
  Direction=Speed>1.f ? FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X)) : 0.f;
 }
 else { Speed=0.f; Direction=0.f; }
}
