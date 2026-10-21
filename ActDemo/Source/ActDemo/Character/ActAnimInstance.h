#pragma once
#include "Animation/AnimInstance.h"
#include "ActAnimInstance.generated.h"

/** Generic locomotion inputs; combat remains owned by the character's GAS abilities. */
UCLASS()
class ACTDEMO_API UActAnimInstance : public UAnimInstance
{
 GENERATED_BODY()
public:
 virtual void NativeUpdateAnimation(float DeltaSeconds) override;
 UPROPERTY(BlueprintReadOnly, Category="Locomotion") float Speed=0.f;
 UPROPERTY(BlueprintReadOnly, Category="Locomotion") float Direction=0.f;
};
