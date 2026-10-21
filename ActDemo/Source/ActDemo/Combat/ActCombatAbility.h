#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Combat/ActCombatTypes.h"
#include "ActCombatAbility.generated.h"

UCLASS(Abstract)
class ACTDEMO_API UActCombatAbility : public UGameplayAbility
{
 GENERATED_BODY()
public:
 UActCombatAbility();
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) EActAction Action = EActAction::None;
 virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
 const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
 FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
 virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
 const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
 virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
 const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
 void Finish();
};
UCLASS() class ACTDEMO_API UActLightAbility : public UActCombatAbility { GENERATED_BODY() public: UActLightAbility(); };
UCLASS() class ACTDEMO_API UActHeavyAbility : public UActCombatAbility { GENERATED_BODY() public: UActHeavyAbility(); };
UCLASS() class ACTDEMO_API UActDodgeAbility : public UActCombatAbility { GENERATED_BODY() public: UActDodgeAbility(); };
UCLASS() class ACTDEMO_API UActHealAbility : public UActCombatAbility { GENERATED_BODY() public: UActHealAbility(); };
