#pragma once

#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ExpeditionAbility.generated.h"

class AVoxelCharacter;
class AIslandPlayerController;
class AIslandGameMode;

UENUM()
enum class EExpeditionAction : uint8
{
    Dig, Throw, Jump, Interact, Rally, Recall
};

namespace ExpeditionAbilities
{
    FGameplayTag ActionTag(EExpeditionAction Action);
    FGameplayTag Stunned();
    FGameplayTag Immune();
    void Grant(UAbilitySystemComponent& ASC);
    void ClearTimedTags(UAbilitySystemComponent& ASC);
    bool ApplyTimedTag(UAbilitySystemComponent& ASC, FGameplayTag Tag, float Seconds);
}

// One replicated duration effect is reused for stun, immunity and action cooldowns.
// Each spec carries the relevant granted tag and its own duration.
UCLASS()
class UExpeditionTimedEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UExpeditionTimedEffect();
};

UCLASS(Abstract)
class UExpeditionAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UExpeditionAbility();
    virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
        FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

protected:
    void SetAction(EExpeditionAction InAction);
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* Payload) override;
    virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
        const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

    virtual void Perform(AVoxelCharacter& Hero, AIslandPlayerController& PC, const FGameplayEventData* Payload)
        PURE_VIRTUAL(UExpeditionAbility::Perform, );
    EExpeditionAction Action = EExpeditionAction::Dig;
    FTimerHandle Timer;
};

UCLASS(Abstract)
class UExpeditionToolAbility : public UExpeditionAbility
{
    GENERATED_BODY()
protected:
    virtual void Perform(AVoxelCharacter& Hero, AIslandPlayerController& PC, const FGameplayEventData* Payload) override;
    virtual void UseTool(AVoxelCharacter& Hero, AIslandPlayerController& PC, AIslandGameMode& Mode, const FVector& Aim)
        PURE_VIRTUAL(UExpeditionToolAbility::UseTool, );
    FGameplayTag Cooldown;
    float Interval = .5f;

private:
    void Swing();
    bool bRepeat = false;
};

UCLASS()
class UExpeditionDigAbility : public UExpeditionToolAbility
{
    GENERATED_BODY()
public:
    UExpeditionDigAbility();
private:
    void UseTool(AVoxelCharacter& Hero, AIslandPlayerController& PC, AIslandGameMode& Mode, const FVector& Aim) override;
};

UCLASS()
class UExpeditionThrowAbility : public UExpeditionToolAbility
{
    GENERATED_BODY()
public:
    UExpeditionThrowAbility();
private:
    void UseTool(AVoxelCharacter& Hero, AIslandPlayerController& PC, AIslandGameMode& Mode, const FVector& Aim) override;
};

UCLASS()
class UExpeditionInteractAbility : public UExpeditionAbility
{
    GENERATED_BODY()
public:
    UExpeditionInteractAbility();
private:
    void Perform(AVoxelCharacter& Hero, AIslandPlayerController& PC, const FGameplayEventData* Payload) override;
};

UCLASS()
class UExpeditionJumpAbility : public UExpeditionAbility
{
    GENERATED_BODY()
public:
    UExpeditionJumpAbility();
private:
    void Perform(AVoxelCharacter& Hero, AIslandPlayerController& PC, const FGameplayEventData* Payload) override;
};

UCLASS()
class UExpeditionRallyAbility : public UExpeditionAbility
{
    GENERATED_BODY()
public:
    UExpeditionRallyAbility();
private:
    void Perform(AVoxelCharacter& Hero, AIslandPlayerController& PC, const FGameplayEventData* Payload) override;
};

UCLASS()
class UExpeditionRecallAbility : public UExpeditionAbility
{
    GENERATED_BODY()
public:
    UExpeditionRecallAbility();
private:
    void Perform(AVoxelCharacter& Hero, AIslandPlayerController& PC, const FGameplayEventData* Payload) override;
    void CompleteRecall();
};

UCLASS()
class UExpeditionCharacterMovement : public UCharacterMovementComponent
{
    GENERATED_BODY()
public:
    float GetIslandJumpZVelocity() const;
    virtual bool DoJump(bool bReplayingMoves, float DeltaTime) override;
    virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
    virtual float GetMaxSpeed() const override;
};
