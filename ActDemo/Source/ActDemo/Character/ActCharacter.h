#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Combat/ActCombatTypes.h"
#include "ActCharacter.generated.h"

class UAbilitySystemComponent;
class UActAttributeSet;
class UActCombatAbility;
class UCameraComponent;
class USpringArmComponent;
class UWidgetComponent;
struct FOnAttributeChangeData;

UENUM(BlueprintType)
enum class EActFaction : uint8 { Player, Enemy, Training };
UENUM(BlueprintType)
enum class EActEnemyKind : uint8 { Scout, Brute, Boss, Dummy };
DECLARE_MULTICAST_DELEGATE(FActCharacterChanged);
DECLARE_MULTICAST_DELEGATE_OneParam(FActActionEnded, bool);

UCLASS(Blueprintable)
class ACTDEMO_API AActCharacter : public ACharacter, public IAbilitySystemInterface
{
 GENERATED_BODY()
public:
 AActCharacter();
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void Tick(float DeltaSeconds) override;
 virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UAbilitySystemComponent> Abilities;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UActAttributeSet> Attributes;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<USpringArmComponent> CameraBoom;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UCameraComponent> Camera;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Weapon;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UWidgetComponent> LifeBar;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") TObjectPtr<UActCombatConfig> CombatConfig;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") TObjectPtr<UActCharacterProfile> Profile;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") EActFaction Faction = EActFaction::Player;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") EActEnemyKind EnemyKind = EActEnemyKind::Scout;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") float InitialHealth = 150.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") float DamageScale = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat") FText DisplayName;
 UPROPERTY(BlueprintReadOnly) int32 HealingCharges = 3;
 UPROPERTY(BlueprintReadOnly) int32 ComboIndex = 0;
 UPROPERTY(BlueprintReadOnly) EActAction CurrentAction = EActAction::None;
 UPROPERTY(BlueprintReadOnly) TObjectPtr<AActCharacter> LockTarget;
 UPROPERTY(BlueprintReadOnly) bool bBossPhaseTwo = false;
 UPROPERTY(BlueprintReadOnly) bool bDebugCombat = false;
 UPROPERTY(BlueprintReadOnly) FString AIState;
 FActCharacterChanged OnChanged;
 FActActionEnded OnActionEnded;

 UFUNCTION(BlueprintPure) bool IsAlive() const;
 UFUNCTION(BlueprintPure) bool IsBusy() const;
 UFUNCTION(BlueprintPure) float HealthFraction() const;
 UFUNCTION(BlueprintPure) float StaminaFraction() const;
 UFUNCTION(BlueprintPure) bool IsHostileTo(const AActCharacter* Other) const;
 UFUNCTION(BlueprintCallable) bool RequestAbility(FGameplayTag Tag, bool bBuffer = true);
 UFUNCTION(BlueprintCallable) void ToggleLock();
 UFUNCTION(BlueprintCallable) void SwitchLock();
 UFUNCTION(BlueprintCallable) void ClearInput();
 UFUNCTION(BlueprintCallable) void SetSprint(bool bEnabled);
 UFUNCTION(BlueprintCallable) void SetCombatWindow(uint8 Window, bool bEnabled);
 void InitializeCombat();
 bool CanStartAction(EActAction Action) const;
 bool BeginAction(EActAction Action, UActCombatAbility* Ability);
 void EndAction(UActCombatAbility* Ability, bool bCancelled);
 void ReceiveCombatHit(AActCharacter* Source, float Damage, FVector Impact);
 void ApplyResource(TSubclassOf<class UGameplayEffect> Effect, float Amount);
 void RestoreAtCheckpoint();
 void SetLock(AActCharacter* Target);
 bool bSprintHeld = false;
 FVector DesiredMove = FVector::ZeroVector;
private:
 void HealthChanged(const FOnAttributeChangeData& Change);
 void Die();
 void TraceWeapon();
 void UpdateLock(float DeltaSeconds);
 FActAttack GetAttack() const;
 float ActionCost(EActAction Action) const;
 UPROPERTY() TObjectPtr<UActCombatAbility> ActiveAbility;
 TSet<TWeakObjectPtr<AActCharacter>> HitActors;
 FGameplayTag BufferedInput;
 float BufferTime = -100.f;
 float ActionStart = 0.f;
 float ActionDuration = 0.f;
 float LastStaminaUse = -100.f;
 float StaggerUntil = 0.f;
 float LostLockTime = 0.f;
 float ComboExpireTime = 0.f;
 float ResourceAccumulator = 0.f;
 float CameraImpactUntil = 0.f;
 bool bDamageWindow = false;
 bool bComboWindow = false;
 bool bActionHasMontage = false;
 bool bInitialized = false;
 bool bHealApplied = false;
 FVector DodgeDirection;
 FVector PreviousTip;
 FVector PreviousBase;
};
