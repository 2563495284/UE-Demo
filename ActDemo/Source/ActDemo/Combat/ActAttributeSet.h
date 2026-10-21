#pragma once
#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "ActAttributeSet.generated.h"

#define ACT_ATTRIBUTE(Name) \
 GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UActAttributeSet, Name) \
 GAMEPLAYATTRIBUTE_VALUE_GETTER(Name) \
 GAMEPLAYATTRIBUTE_VALUE_SETTER(Name) \
 GAMEPLAYATTRIBUTE_VALUE_INITTER(Name)

UCLASS()
class ACTDEMO_API UActAttributeSet : public UAttributeSet
{
 GENERATED_BODY()
public:
 UPROPERTY(BlueprintReadOnly) FGameplayAttributeData Health;
 UPROPERTY(BlueprintReadOnly) FGameplayAttributeData MaxHealth;
 UPROPERTY(BlueprintReadOnly) FGameplayAttributeData Stamina;
 UPROPERTY(BlueprintReadOnly) FGameplayAttributeData MaxStamina;
 ACT_ATTRIBUTE(Health)
 ACT_ATTRIBUTE(MaxHealth)
 ACT_ATTRIBUTE(Stamina)
 ACT_ATTRIBUTE(MaxStamina)
 virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
 virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
};

UCLASS()
class ACTDEMO_API UActHealthEffect : public UGameplayEffect
{
 GENERATED_BODY()
public: UActHealthEffect();
};
UCLASS()
class ACTDEMO_API UActStaminaEffect : public UGameplayEffect
{
 GENERATED_BODY()
public: UActStaminaEffect();
};
