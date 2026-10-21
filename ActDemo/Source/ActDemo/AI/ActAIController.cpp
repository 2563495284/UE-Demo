#include "AI/ActAIController.h"
#include "Character/ActCharacter.h"
#include "Combat/ActAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/StateTreeAIComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "StateTreeExecutionContext.h"
#include "StateTree.h"

AActAIController::AActAIController()
{
 StateTree = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTree"));
 StateTree->SetStartLogicAutomatically(false);
 bStartAILogicOnPossess = false;
 bAttachToPawn = true;
 Senses = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
 SetPerceptionComponent(*Senses);
 auto* Sight = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight"));
 Sight->SightRadius = 1200.f; Sight->LoseSightRadius = 1500.f; Sight->PeripheralVisionAngleDegrees = 65.f;
 Sight->SetMaxAge(4.f);
 Sight->DetectionByAffiliation.bDetectEnemies = true;
 Sight->DetectionByAffiliation.bDetectNeutrals = true;
 Sight->DetectionByAffiliation.bDetectFriendlies = false;
 Senses->ConfigureSense(*Sight);
 Senses->SetDominantSense(Sight->GetSenseImplementation());
 auto* Hearing = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("Hearing"));
 Hearing->HearingRange = 1300.f; Hearing->SetMaxAge(3.f);
 Hearing->DetectionByAffiliation.bDetectEnemies = true;
 Hearing->DetectionByAffiliation.bDetectNeutrals = true;
 Senses->ConfigureSense(*Hearing);
 SetGenericTeamId(FGenericTeamId(1));
}
void AActAIController::OnPossess(APawn* Pawn)
{
 Super::OnPossess(Pawn);
 auto* Character = Cast<AActCharacter>(Pawn);
 if (!Character || Character->Faction != EActFaction::Enemy) return;
 Character->InitializeCombat();
 if (const auto* Profile=Character->Profile.Get())
 {
  PatrolRadius=Profile->PatrolRadius; LeashRadius=Profile->LeashRadius; SearchDuration=Profile->SearchDuration;
  auto* Sight=CastChecked<UAISenseConfig_Sight>(Senses->GetSenseConfig(UAISense::GetSenseID<UAISense_Sight>()));
  Sight->SightRadius=Profile->SightRadius; Sight->LoseSightRadius=Profile->SightRadius+300.f; Senses->ConfigureSense(*Sight);
  auto* Hearing=CastChecked<UAISenseConfig_Hearing>(Senses->GetSenseConfig(UAISense::GetSenseID<UAISense_Hearing>()));
  Hearing->HearingRange=Profile->HearingRadius; Senses->ConfigureSense(*Hearing);
 }
 Home = Pawn->GetActorLocation();
 Senses->OnTargetPerceptionUpdated.AddDynamic(this, &AActAIController::Perceived);
 if (!Behavior) Behavior = LoadObject<UStateTree>(nullptr, TEXT("/Game/ActDemo/AI/ST_Enemy.ST_Enemy"));
 if (Behavior) { StateTree->SetStateTree(Behavior); StateTree->StartLogic(); }
}
void AActAIController::OnUnPossess()
{
 StopCombat(); Senses->OnTargetPerceptionUpdated.RemoveAll(this); Super::OnUnPossess();
}
ETeamAttitude::Type AActAIController::GetTeamAttitudeTowards(const AActor& Other) const
{
 const auto* Character = Cast<AActCharacter>(&Other);
 return Character && Character->Faction == EActFaction::Player ? ETeamAttitude::Hostile : ETeamAttitude::Friendly;
}
void AActAIController::Perceived(AActor* Actor, FAIStimulus Stimulus)
{
 auto* Character = Cast<AActCharacter>(Actor);
 if (!Character || Character->Faction != EActFaction::Player || !Character->IsAlive()) return;
 const bool bSight = Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>();
 if (Stimulus.WasSuccessfullySensed())
 {
  Target = Character; LastKnownLocation = Stimulus.StimulusLocation;
  if (bSight) bSeesTarget = true;
  bInvestigating = true; SearchUntil = 0.f;
 }
 else if (bSight && Target == Character) { bSeesTarget = false; bInvestigating = true; }
}
void AActAIController::NotifyDamage(AActCharacter* Source)
{
 if (!Source || Source->Faction != EActFaction::Player) return;
 Target = Source; LastKnownLocation = Source->GetActorLocation();
 DamageAwarenessUntil = GetWorld()->GetTimeSeconds() + 2.f; bInvestigating = true;
}
EActAIState AActAIController::DesiredState() const
{
 const auto* Character = Cast<AActCharacter>(GetPawn());
 if (!Character || !Character->IsAlive()) return EActAIState::Dead;
 const float Now = GetWorld()->GetTimeSeconds();
 if (Character->CurrentAction != EActAction::None) return EActAIState::Attack;
 if (Now < RecoveryUntil || Character->IsBusy()) return EActAIState::Recover;
 if (FVector::DistSquared(Home, Character->GetActorLocation()) > FMath::Square(LeashRadius)) return EActAIState::Return;
 if (IsValid(Target) && Target->IsAlive() && (bSeesTarget || Now < DamageAwarenessUntil))
 {
  if (FVector::DistSquared(Character->GetActorLocation(), Target->GetActorLocation()) < FMath::Square(170.f))
   return Character->Attributes->GetStamina() >= 36.f ? EActAIState::Attack : EActAIState::Recover;
  return EActAIState::Chase;
 }
 if (bInvestigating) return EActAIState::Investigate;
 if (Now < SearchUntil) return EActAIState::Search;
 if (Target || FVector::DistSquared(Home, Character->GetActorLocation()) > FMath::Square(PatrolRadius + 100.f)) return EActAIState::Return;
 return EActAIState::Patrol;
}
void AActAIController::EnterBehavior(EActAIState NewState)
{
 State = NewState; StateStart = GetWorld()->GetTimeSeconds(); LastMoveRequest = -100.f;
 auto* Character = Cast<AActCharacter>(GetPawn());
 if (!Character) return;
 Character->AIState = UEnum::GetValueAsString(State);
 if (State == EActAIState::Attack) BeginAttack();
 else if (State == EActAIState::Recover)
 { StopMovement(); RecoveryUntil = FMath::Max(RecoveryUntil, StateStart + .5f); }
 else if (State == EActAIState::Patrol)
 {
  FNavLocation Point;
  auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
  MoveGoal = Nav && Nav->GetRandomReachablePointInRadius(Home, PatrolRadius, Point) ? Point.Location : Home;
  MoveToLocation(MoveGoal, 65.f);
 }
 else if (State == EActAIState::Investigate) { MoveGoal = LastKnownLocation; MoveToLocation(MoveGoal, 90.f); }
 else if (State == EActAIState::Return)
 {
  Target = nullptr; bSeesTarget = bInvestigating = false; SearchUntil = DamageAwarenessUntil = 0.f;
  ClearFocus(EAIFocusPriority::Gameplay); MoveGoal = Home; MoveToLocation(Home, 80.f);
 }
 else if (State == EActAIState::Search) { StopMovement(); ClearFocus(EAIFocusPriority::Gameplay); }
}
bool AActAIController::TickBehavior(EActAIState Expected, float Dt)
{
 auto* Character = Cast<AActCharacter>(GetPawn());
 if (!Character) return true;
 const float Now = GetWorld()->GetTimeSeconds();
 if (Expected == EActAIState::Attack)
 {
  if (!IsValid(Target) || !Target->IsAlive()) return true;
  return bAttackFinished || !Character->IsAlive();
 }
 if (DesiredState() != Expected) return true;
 if (Expected == EActAIState::Chase && Target)
 {
  SetFocus(Target);
  LastKnownLocation = Target->GetActorLocation();
  if (Now - LastMoveRequest > .4f)
  {
   // Keep the stop distance inside melee range after the target capsule is included.
   const auto Result = MoveToActor(Target, 95.f, false);
   LastMoveRequest = Now;
   if (Result == EPathFollowingRequestResult::Failed || Now-StateStart > 12.f)
   { bSeesTarget = false; bInvestigating = false; Target = nullptr; RecoveryUntil = Now + 1.f; return true; }
  }
 }
 if (Expected == EActAIState::Investigate && (GetMoveStatus() == EPathFollowingStatus::Idle || Now-StateStart > 6.f))
 { bInvestigating = false; SearchUntil = Now + SearchDuration; return true; }
 if (Expected == EActAIState::Patrol && Now-StateStart > 3.f) return true;
 if (Expected == EActAIState::Return && (GetMoveStatus() == EPathFollowingStatus::Idle || Now-StateStart > 8.f))
 { Home = Character->GetActorLocation(); return true; }
 if (Expected == EActAIState::Search) Character->AddActorWorldRotation(FRotator(0, 50.f*Dt, 0));
 return false;
}
void AActAIController::BeginAttack()
{
 auto* Character = Cast<AActCharacter>(GetPawn());
 bAttackFinished = false; StopMovement();
 if (!Character || !IsValid(Target) || !Target->IsAlive()) { bAttackFinished = true; return; }
 FRotator Face = (Target->GetActorLocation()-Character->GetActorLocation()).Rotation(); Face.Pitch = 0;
 Character->SetActorRotation(Face);
 ++AttackCount;
 const int32 HeavyInterval = Character->bBossPhaseTwo ? 2 : 3;
 AttackTag = Character->EnemyKind == EActEnemyKind::Brute || (Character->EnemyKind == EActEnemyKind::Boss && AttackCount % HeavyInterval == 0) ? ActTags::Heavy : ActTags::Light;
 AttackDelegate = Character->OnActionEnded.AddUObject(this, &AActAIController::AttackEnded);
 bOwnsAttack = Character->RequestAbility(AttackTag, false);
 if (!bOwnsAttack) AttackEnded(true);
}
void AActAIController::AttackEnded(bool bCancelled)
{
 bAttackFinished = true;
 const auto* Character = Cast<AActCharacter>(GetPawn());
 const float Delay = Character && Character->bBossPhaseTwo ? .35f : .9f;
 RecoveryUntil = GetWorld()->GetTimeSeconds() + (bCancelled ? .6f : Delay);
}
void AActAIController::ExitBehavior(EActAIState Exiting)
{
 StopMovement();
 if (Exiting == EActAIState::Attack)
 {
  if (auto* Character = Cast<AActCharacter>(GetPawn()))
  {
   Character->OnActionEnded.Remove(AttackDelegate);
   if (bOwnsAttack && !bAttackFinished) { const FGameplayTagContainer Tags(AttackTag); Character->Abilities->CancelAbilities(&Tags); }
  }
  bOwnsAttack = false;
 }
}
void AActAIController::StopCombat()
{
 ExitBehavior(State); StateTree->StopLogic(TEXT("Combat ended")); StopMovement(); ClearFocus(EAIFocusPriority::Gameplay); Target = nullptr;
}
static AActAIController* ActController(FStateTreeExecutionContext& Context) { return Cast<AActAIController>(static_cast<UObject*>(Context.GetOwner())); }
bool FActAICondition::TestCondition(FStateTreeExecutionContext& Context) const
{ const auto* AI = ActController(Context); return AI && AI->DesiredState() == Context.GetInstanceData(*this).State; }
EStateTreeRunStatus FActAIStateTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{
 auto* AI = ActController(Context); if (!AI) return EStateTreeRunStatus::Failed;
 AI->EnterBehavior(Context.GetInstanceData(*this).State); return EStateTreeRunStatus::Running;
}
EStateTreeRunStatus FActAIStateTask::Tick(FStateTreeExecutionContext& Context, const float Dt) const
{ auto* AI = ActController(Context); return !AI || AI->TickBehavior(Context.GetInstanceData(*this).State, Dt) ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Running; }
void FActAIStateTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{ if (auto* AI = ActController(Context)) AI->ExitBehavior(Context.GetInstanceData(*this).State); }
