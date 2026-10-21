#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NativeGameplayTags.h"
#include "ActCombatTypes.generated.h"

class UAnimMontage;
class UNiagaraSystem;
class USoundBase;
class UInputAction;
class UInputMappingContext;
class USkeletalMesh;
class UAnimInstance;

namespace ActTags
{
 ACTDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Light);
 ACTDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Heavy);
 ACTDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Dodge);
 ACTDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Heal);
 ACTDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Busy);
 ACTDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Invulnerable);
 ACTDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Dead);
 ACTDEMO_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Magnitude);
}

UENUM(BlueprintType)
enum class EActAction : uint8 { None, Light, Heavy, Dodge, Heal };

USTRUCT(BlueprintType)
struct FActAttack
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UAnimMontage> Montage;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1")) float Duration = 0.85f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float Damage = 26.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float StaminaCost = 18.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float Reach = 185.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float ActiveStart = 0.28f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float ActiveEnd = 0.5f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float ComboStart = 0.48f;
};

/** One weapon definition is shared by human and AI ability requests. Times are seconds. */
UCLASS(BlueprintType)
class ACTDEMO_API UActCombatConfig : public UDataAsset
{
 GENERATED_BODY()
public:
 UActCombatConfig();
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FActAttack> LightAttacks;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FActAttack HeavyAttack;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimMontage> DodgeMontage;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimMontage> HealMontage;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimMontage> HitMontage;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UAnimMontage> DeathMontage;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UNiagaraSystem> HitEffect;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> HitSound;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USoundBase> AttackSound;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float DodgeCost = 25.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float DodgeDuration = 0.6f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float DodgeSpeed = 900.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float InvulnerableStart = 0.08f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float InvulnerableEnd = 0.34f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float StaminaRegen = 26.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float RegenDelay = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float InputBufferSeconds = 0.25f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float HealAmount = 60.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float HealDuration = 1.25f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 HealCharges = 3;
};

/** Per-archetype tuning. Optional visuals allow compatible skeletons to be swapped in Blueprint. */
UCLASS(BlueprintType)
class ACTDEMO_API UActCharacterProfile : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) float Health = 150.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float DamageScale = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float MoveSpeed = 420.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float SightRadius = 1200.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float HearingRadius = 1300.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float PatrolRadius = 350.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float LeashRadius = 2000.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float SearchDuration = 4.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UActCombatConfig> Combat;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<USkeletalMesh> Mesh;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TSubclassOf<UAnimInstance> AnimClass;
 /** Mesh-integrated weapons use these sockets instead of the graybox blade/corridor. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FName WeaponBaseSocket;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FName WeaponTipSocket;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) float WeaponTraceRadius=18.f;
};

UCLASS(BlueprintType)
class ACTDEMO_API UActInputConfig : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputMappingContext> Gameplay;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputMappingContext> Menu;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Move;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Look;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Sprint;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Light;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Heavy;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Dodge;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Heal;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Lock;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Switch;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Pause;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Restart;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UInputAction> Debug;
};
