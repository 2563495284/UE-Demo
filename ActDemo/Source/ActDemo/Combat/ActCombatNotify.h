#pragma once
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ActCombatNotify.generated.h"

/** 0: weapon hit window, 1: dodge invulnerability, 2: combo acceptance. */
UCLASS(meta=(DisplayName="Act Combat Window"))
class ACTDEMO_API UActCombatNotify : public UAnimNotifyState
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere, Category="Combat", meta=(ClampMin="0", ClampMax="2")) uint8 Window = 0;
 virtual void NotifyBegin(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation, float Duration, const FAnimNotifyEventReference& Reference) override;
 virtual void NotifyEnd(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& Reference) override;
};
