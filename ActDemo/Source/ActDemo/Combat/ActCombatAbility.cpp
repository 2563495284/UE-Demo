#include "Combat/ActCombatAbility.h"
#include "Character/ActCharacter.h"

UActCombatAbility::UActCombatAbility()
{
 InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
 NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
}
bool UActCombatAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
 const FGameplayTagContainer* Source, const FGameplayTagContainer* Target, FGameplayTagContainer* Relevant) const
{
 const AActCharacter* Character = Info ? Cast<AActCharacter>(Info->AvatarActor.Get()) : nullptr;
 return Character && Character->CanStartAction(Action) && Super::CanActivateAbility(Handle, Info, Source, Target, Relevant);
}
void UActCombatAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
 const FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
 AActCharacter* Character = Cast<AActCharacter>(Info->AvatarActor.Get());
 if (!Character || !Character->BeginAction(Action, this)) EndAbility(Handle, Info, Activation, false, true);
}
void UActCombatAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
 const FGameplayAbilityActivationInfo Activation, bool bReplicate, bool bCancelled)
{
 if (AActCharacter* Character = Info ? Cast<AActCharacter>(Info->AvatarActor.Get()) : nullptr) Character->EndAction(this, bCancelled);
 Super::EndAbility(Handle, Info, Activation, bReplicate, bCancelled);
}
void UActCombatAbility::Finish() { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false); }
UActLightAbility::UActLightAbility() { Action = EActAction::Light; SetAssetTags(FGameplayTagContainer(ActTags::Light)); }
UActHeavyAbility::UActHeavyAbility() { Action = EActAction::Heavy; SetAssetTags(FGameplayTagContainer(ActTags::Heavy)); }
UActDodgeAbility::UActDodgeAbility() { Action = EActAction::Dodge; SetAssetTags(FGameplayTagContainer(ActTags::Dodge)); }
UActHealAbility::UActHealAbility() { Action = EActAction::Heal; SetAssetTags(FGameplayTagContainer(ActTags::Heal)); }
