#include "Abilities/ExpeditionMudEffect.h"

#include "Abilities/ExpeditionAbility.h"
#include "AbilitySystemComponent.h"
#include "NativeGameplayTags.h"
#include "Voxel/VoxelCharacter.h"

namespace
{
    UE_DEFINE_GAMEPLAY_TAG_STATIC(MudImpactWindow, "Expedition.Status.MudImpactWindow");
}

UExpeditionMudMarkEffect::UExpeditionMudMarkEffect()
{
    DurationPolicy = EGameplayEffectDurationType::HasDuration;
    DurationMagnitude = FScalableFloat(1.5f);
    StackingType = EGameplayEffectStackingType::AggregateByTarget;
    StackLimitCount = 3;
    StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
    StackExpirationPolicy = EGameplayEffectStackingExpirationPolicy::ClearEntireStack;
}

int32 UExpeditionMudMarkEffect::Count(const AVoxelCharacter& Target)
{
    return Target.GetAbilitySystemComponent()->GetGameplayEffectCount(StaticClass(), nullptr);
}

int32 UExpeditionMudMarkEffect::TryHit(AVoxelCharacter& Thrower, AVoxelCharacter& Target)
{
    UAbilitySystemComponent& Victim = *Target.GetAbilitySystemComponent();
    if (Target.IsMudStunned() || Target.IsMudImmune() || Victim.HasMatchingGameplayTag(MudImpactWindow)) return 0;
    UAbilitySystemComponent& Source = *Thrower.GetAbilitySystemComponent();
    const FActiveGameplayEffectHandle Marks = Victim.ApplyGameplayEffectToSelf(GetDefault<UExpeditionMudMarkEffect>(),
        1.f, Source.MakeEffectContext());
    if (!Marks.IsValid()) return 0;
    ExpeditionAbilities::ApplyTimedTag(Victim, MudImpactWindow, .4f);
    const int32 Stack = Victim.GetCurrentStackCount(Marks);
    if (Stack == 3) Victim.RemoveActiveGameplayEffect(Marks);
    return Stack;
}

void UExpeditionMudMarkEffect::Clear(AVoxelCharacter& Target)
{
    UAbilitySystemComponent& ASC = *Target.GetAbilitySystemComponent();
    FGameplayEffectQuery Query;
    Query.EffectDefinition = StaticClass();
    ASC.RemoveActiveEffects(Query);
    ASC.RemoveActiveEffects(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(MudImpactWindow)));
}
