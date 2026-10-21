#include "Combat/ActAttributeSet.h"
#include "Combat/ActCombatTypes.h"
#include "GameplayEffectExtension.h"

void UActAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& Value)
{
 Super::PreAttributeChange(Attribute, Value);
 if (Attribute == GetHealthAttribute()) Value = FMath::Clamp(Value, 0.f, GetMaxHealth());
 if (Attribute == GetStaminaAttribute()) Value = FMath::Clamp(Value, 0.f, GetMaxStamina());
}
void UActAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
 Super::PostGameplayEffectExecute(Data);
 if (Data.EvaluatedData.Attribute == GetHealthAttribute()) SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
 if (Data.EvaluatedData.Attribute == GetStaminaAttribute()) SetStamina(FMath::Clamp(GetStamina(), 0.f, GetMaxStamina()));
}
static void AddMagnitudeModifier(UGameplayEffect* Effect, FGameplayAttribute Attribute)
{
 Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
 FGameplayModifierInfo Modifier;
 Modifier.Attribute = Attribute;
 Modifier.ModifierOp = EGameplayModOp::Additive;
 FSetByCallerFloat Magnitude;
 Magnitude.DataTag = ActTags::Magnitude;
 Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
 Effect->Modifiers.Add(Modifier);
}
UActHealthEffect::UActHealthEffect() { AddMagnitudeModifier(this, UActAttributeSet::GetHealthAttribute()); }
UActStaminaEffect::UActStaminaEffect() { AddMagnitudeModifier(this, UActAttributeSet::GetStaminaAttribute()); }
