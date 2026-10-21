#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "ActAIController.generated.h"
class AActCharacter;
class UStateTreeAIComponent;
class UStateTree;
class UAIPerceptionComponent;

UENUM(BlueprintType)
enum class EActAIState : uint8 { Patrol, Investigate, Chase, Attack, Recover, Search, Return, Dead };

UCLASS(Blueprintable)
class ACTDEMO_API AActAIController : public AAIController
{
 GENERATED_BODY()
public:
 AActAIController();
 virtual void OnPossess(APawn* Pawn) override;
 virtual void OnUnPossess() override;
 virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStateTreeAIComponent> StateTree;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UAIPerceptionComponent> Senses;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TObjectPtr<UStateTree> Behavior;
 UPROPERTY(BlueprintReadOnly) TObjectPtr<AActCharacter> Target;
 UPROPERTY(BlueprintReadOnly) FVector LastKnownLocation;
 UPROPERTY(BlueprintReadOnly) FVector Home;
 UPROPERTY(BlueprintReadOnly) EActAIState State = EActAIState::Patrol;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI") float PatrolRadius = 350.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI") float LeashRadius = 2000.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI") float SearchDuration = 4.f;
 void NotifyDamage(AActCharacter* Source);
 void StopCombat();
 EActAIState DesiredState() const;
 void EnterBehavior(EActAIState NewState);
 bool TickBehavior(EActAIState Expected, float Dt);
 void ExitBehavior(EActAIState Exiting);
private:
 UFUNCTION() void Perceived(AActor* Actor, FAIStimulus Stimulus);
 void BeginAttack();
 void AttackEnded(bool bCancelled);
 bool bSeesTarget = false;
 bool bInvestigating = false;
 bool bOwnsAttack = false;
 bool bAttackFinished = false;
 float SearchUntil = 0.f;
 float RecoveryUntil = 0.f;
 float DamageAwarenessUntil = 0.f;
 float StateStart = 0.f;
 float LastMoveRequest = -100.f;
 int32 AttackCount = 0;
 FVector MoveGoal;
 FDelegateHandle AttackDelegate;
 FGameplayTag AttackTag;
};

USTRUCT()
struct FActAIStateData
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, Category="Parameter") EActAIState State = EActAIState::Patrol;
};
USTRUCT(meta=(DisplayName="Act: State is Requested"))
struct ACTDEMO_API FActAICondition : public FStateTreeConditionCommonBase
{
 GENERATED_BODY()
 using FInstanceDataType = FActAIStateData;
 virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
 virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
USTRUCT(meta=(DisplayName="Act: Execute AI State"))
struct ACTDEMO_API FActAIStateTask : public FStateTreeTaskCommonBase
{
 GENERATED_BODY()
 using FInstanceDataType = FActAIStateData;
 virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
 virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const override;
 virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float Dt) const override;
 virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const override;
};
