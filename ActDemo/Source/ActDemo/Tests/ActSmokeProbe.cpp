#include "Tests/ActSmokeProbe.h"
#include "Character/ActCharacter.h"
#include "Character/ActPlayerController.h"
#include "Combat/ActAttributeSet.h"
#include "AI/ActAIController.h"
#include "World/ActGameMode.h"
#include "AbilitySystemComponent.h"
#include "Components/StateTreeAIComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Components/BrushComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"

AActSmokeProbe::AActSmokeProbe() { PrimaryActorTick.bCanEverTick=true; }
void AActSmokeProbe::Check(bool Condition,const TCHAR* Message)
{
 ++Checks;
 if (!Condition) { ++Failures; UE_LOG(LogTemp,Error,TEXT("ACT_TEST FAIL: %s"),Message); }
 else UE_LOG(LogTemp,Display,TEXT("ACT_TEST PASS: %s"),Message);
}
void AActSmokeProbe::Next(float Delay) { ++Stage; Until=GetWorld()->GetTimeSeconds()+Delay; }
void AActSmokeProbe::StopEnemies()
{ for(TActorIterator<AActAIController> It(GetWorld());It;++It) It->StopCombat(); }
void AActSmokeProbe::Tick(float Dt)
{
 Super::Tick(Dt);
 if (GetWorld()->GetTimeSeconds()<Until) return;
 auto* Mode=GetWorld()->GetAuthGameMode<AActGameMode>();
 if (!Mode) { Check(false,TEXT("GameMode exists")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
 if (Stage==0)
 {
  Hero=Cast<AActCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
  Check(Hero.IsValid(),TEXT("Player spawned"));
  if (!Hero.IsValid()) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
  int32 Running=0; for(TActorIterator<AActAIController> It(GetWorld());It;++It) if(It->StateTree->IsRunning()) ++Running;
  Check(Running==6,TEXT("Six enemy StateTrees are running"));
  FNavLocation Location; auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
  Check(Nav && Nav->ProjectPointToNavigation(Hero->GetActorLocation(),Location),TEXT("Arena has usable navigation"));
  for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It)
   UE_LOG(LogTemp,Display,TEXT("ACT_DIAG navigation extent=%s perception=%s"),*It->GetBrushComponent()->Bounds.BoxExtent.ToString(),*GetNameSafe(UAIPerceptionSystem::GetCurrent(GetWorld())));
  StopEnemies();
  for(TActorIterator<AActCharacter> It(GetWorld());It;++It) if(It->Faction==EActFaction::Enemy && It->EnemyKind==EActEnemyKind::Scout) { Enemy=*It; break; }
  Check(Enemy.IsValid(),TEXT("Scout exists"));
  if(!Enemy.IsValid()) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
  Hero->SetActorLocation(FVector(-4600,0,100)); Hero->SetActorRotation(FRotator::ZeroRotator);
  Enemy->SetActorLocation(FVector(-4460,0,100));
  Hero->ApplyResource(UActStaminaEffect::StaticClass(),-100);
  Check(!Hero->RequestAbility(ActTags::Heavy,false),TEXT("Insufficient stamina rejects heavy attack"));
  Hero->ApplyResource(UActStaminaEffect::StaticClass(),100);
  Check(Hero->RequestAbility(ActTags::Light,false),TEXT("Light attack activates GAS ability"));
  Check(!Hero->RequestAbility(ActTags::Heavy,false),TEXT("Busy state rejects overlapping ability"));
  PreviousHealth=Enemy->Attributes->GetHealth(); Next(1.1f);
 }
 else if(Stage==1)
 {
  Check(FMath::IsNearlyEqual(PreviousHealth-Enemy->Attributes->GetHealth(),26.f,.1f),TEXT("One swing damages each target exactly once"));
  Check(!Hero->IsBusy(),TEXT("Attack exits busy state"));
  Hero->ApplyResource(UActStaminaEffect::StaticClass(),100);
  Check(Hero->RequestAbility(ActTags::Dodge,false),TEXT("Dodge activates")); Next(.17f);
 }
 else if(Stage==2)
 {
  Check(Hero->Abilities->HasMatchingGameplayTag(ActTags::Invulnerable),TEXT("Dodge notify enables invulnerability"));
  PreviousHealth=Hero->Attributes->GetHealth(); Hero->ReceiveCombatHit(Enemy.Get(),40,Hero->GetActorLocation());
  Check(FMath::IsNearlyEqual(PreviousHealth,Hero->Attributes->GetHealth()),TEXT("Dodge window prevents damage")); Next(.7f);
 }
 else if(Stage==3)
 {
  Check(!Hero->Abilities->HasMatchingGameplayTag(ActTags::Invulnerable),TEXT("Dodge clears invulnerability"));
  Hero->SetActorLocation(FVector(-4600,0,100)); Hero->SetActorRotation(FRotator::ZeroRotator); Enemy->SetActorLocation(FVector(-4460,0,100));
  Hero->RequestAbility(ActTags::Light,false); Hero->Abilities->CancelAllAbilities(); PreviousHealth=Enemy->Attributes->GetHealth(); Next(1.f);
 }
 else if(Stage==4)
 {
  Check(FMath::IsNearlyEqual(PreviousHealth,Enemy->Attributes->GetHealth()),TEXT("Cancelled attack leaves no damage window"));
  Check(!Hero->Abilities->HasMatchingGameplayTag(ActTags::Busy),TEXT("Cancellation removes busy tag"));
  Hero->ApplyResource(UActHealthEffect::StaticClass(),-80); Check(Hero->RequestAbility(ActTags::Heal,false),TEXT("Recovery item activates")); Next(1.5f);
 }
 else if(Stage==5)
 {
  Check(Hero->HealingCharges==2 && FMath::IsNearlyEqual(Hero->Attributes->GetHealth(),130.f),TEXT("Flask consumes one charge and restores health"));
  Hero->SetLock(Enemy.Get()); Enemy->ApplyResource(UActHealthEffect::StaticClass(),-10000); Next(.2f);
 }
 else if(Stage==6)
 {
  Check(!Hero->LockTarget,TEXT("Dead target releases lock"));
  Mode->ActivateCheckpoint(FVector(0,0,100));
  Check(Mode->bCheckpoint && Hero->HealingCharges==3 && Hero->HealthFraction()==1.f,TEXT("Checkpoint restores resources"));
  Hero->RequestAbility(ActTags::Light,false); Hero->ApplyResource(UActHealthEffect::StaticClass(),-10000); Next(.2f);
 }
 else if(Stage==7)
 {
  Check(Mode->Outcome==EActOutcome::Defeat && !Hero->IsBusy(),TEXT("Death cancels ability and enters defeat"));
  CastChecked<AActPlayerController>(UGameplayStatics::GetPlayerController(this,0))->Retry();
  Hero=Cast<AActCharacter>(UGameplayStatics::GetPlayerPawn(this,0)); StopEnemies(); Next(.2f);
 }
 else if(Stage==8)
 {
  Check(Hero.IsValid() && Hero->IsAlive() && Hero->GetActorLocation().X>-500 && Mode->Outcome==EActOutcome::Playing,TEXT("Retry respawns at checkpoint"));
  int32 Enemies=0; for(TActorIterator<AActCharacter> It(GetWorld());It;++It) if(It->Faction==EActFaction::Enemy && !It->IsActorBeingDestroyed()) ++Enemies;
  Check(Enemies==3,TEXT("Retry recreates remaining encounters without duplication"));
  Mode->Boss->ApplyResource(UActHealthEffect::StaticClass(),-500);
  Check(Mode->Boss->bBossPhaseTwo,TEXT("Boss changes phase below half health"));
  Mode->Boss->ApplyResource(UActHealthEffect::StaticClass(),-10000); Check(Mode->Outcome==EActOutcome::Victory,TEXT("Boss death completes run"));
  CastChecked<AActPlayerController>(UGameplayStatics::GetPlayerController(this,0))->RestartRun(); StopEnemies(); Next(.2f);
 }
 else if(Stage==9)
 {
  Check(!Mode->bCheckpoint && Mode->Outcome==EActOutcome::Playing,TEXT("New run clears checkpoint and victory"));
  int32 Controllers=0; for(TActorIterator<AActAIController> It(GetWorld());It;++It) if(!It->IsActorBeingDestroyed()) ++Controllers;
  Check(Controllers==6,TEXT("Restart does not leak AI controllers"));
  Hero=Cast<AActCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
  auto* PC=CastChecked<AActPlayerController>(Hero->GetController());
  Hero->RequestAbility(ActTags::Light,false); Hero->RequestAbility(ActTags::Heavy,true);
  PC->TogglePause();
  Check(!Hero->RequestAbility(ActTags::Dodge),TEXT("Paused world rejects combat input"));
  PC->TogglePause(); Next(1.2f);
 }
 else if(Stage==10)
 {
  Check(!Hero->IsBusy(),TEXT("Resume discards buffered actions"));
  Hero->RestoreAtCheckpoint();
  for(TActorIterator<AActCharacter> It(GetWorld());It;++It) if(It->Faction==EActFaction::Enemy && It->EnemyKind==EActEnemyKind::Scout) { Enemy=*It; break; }
  Hero->SetActorLocation(FVector(-4600,0,100));
  Enemy->SetActorLocation(FVector(-4000,0,100)); Enemy->SetActorRotation(FRotator(0,180,0));
  auto* AI=CastChecked<AActAIController>(Enemy->GetController()); AI->Home=Enemy->GetActorLocation();
  AI->Senses->ForgetAll(); AI->StateTree->StartLogic(); PreviousHealth=Hero->Attributes->GetHealth(); Next(4.f);
 }
 else if(Stage==11)
 {
  auto* AI=CastChecked<AActAIController>(Enemy->GetController());
  FActorPerceptionBlueprintInfo Perception; AI->Senses->GetActorsPerception(Hero.Get(),Perception);
  UE_LOG(LogTemp,Display,TEXT("ACT_DIAG state=%s hero=%s enemy=%s stimuli=%d target=%s"),*UEnum::GetValueAsString(AI->State),*Hero->GetActorLocation().ToString(),*Enemy->GetActorLocation().ToString(),Perception.LastSensedStimuli.Num(),*GetNameSafe(AI->Target));
  Check(AI->Target==Hero,TEXT("Sight perception identifies the player"));
  Check(FVector::Dist2D(Hero->GetActorLocation(),Enemy->GetActorLocation())<185.f,TEXT("StateTree chase reaches melee range"));
  Check(Hero->Attributes->GetHealth()<PreviousHealth,TEXT("StateTree attack applies damage through GAS"));
  // Cross to the far end of the real arena so sight reports a lost stimulus.
  // Disabling a sense is not equivalent: it does not synthesize a sight-loss event.
  Hero->SetActorLocation(FVector(4700,-1200,100)); Next(7.f);
 }
 else if(Stage==12)
 {
  auto* AI=CastChecked<AActAIController>(Enemy->GetController());
  Check(AI->State==EActAIState::Search || AI->State==EActAIState::Return || AI->State==EActAIState::Patrol,TEXT("Lost sight exits attack and searches or returns"));
  Next(9.f);
 }
 else if(Stage==13)
 {
  auto* AI=CastChecked<AActAIController>(Enemy->GetController());
  Check(!AI->Target && AI->State==EActAIState::Patrol,TEXT("Search times out and returns to patrol"));
  const FVector Noise=Enemy->GetActorLocation()+FVector(400,0,0);
  UAISense_Hearing::ReportNoiseEvent(GetWorld(),Noise,1.f,Hero.Get(),1300.f); Next(.3f);
 }
 else if(Stage==14)
 {
  auto* AI=CastChecked<AActAIController>(Enemy->GetController());
  Check(AI->Target==Hero && AI->State==EActAIState::Investigate,TEXT("Hearing sends StateTree to investigate"));
  StopEnemies(); Hero->SetLock(Enemy.Get()); Enemy->SetActorLocation(Hero->GetActorLocation()+FVector(3000,0,0)); Next(.2f);
 }
 else if(Stage==15)
 {
  Check(!Hero->LockTarget,TEXT("Out of range target releases lock"));
  Hero->RestoreAtCheckpoint(); Enemy->RestoreAtCheckpoint();
  Hero->SetActorLocation(FVector(-4600,0,100)); Hero->SetActorRotation(FRotator::ZeroRotator); Enemy->SetActorLocation(FVector(-4460,0,100));
  Next(.2f);
 }
 else if(Stage==16)
 { PreviousHealth=Enemy->Attributes->GetHealth(); Hero->RequestAbility(ActTags::Light,false); Next(.4f); }
 else if(Stage==17)
 { Hero->RequestAbility(ActTags::Light,true); Next(.52f); }
 else if(Stage==18)
 {
  Check(Hero->ComboIndex==1,TEXT("Buffered light enters second combo section"));
  Hero->RequestAbility(ActTags::Light,true); Next(1.15f);
 }
 else if(Stage==19)
 {
  Check(FMath::IsNearlyEqual(PreviousHealth-Enemy->Attributes->GetHealth(),94.f,.1f),TEXT("Three light attacks apply their three configured damage values"));
  Check(!Hero->IsBusy() && Hero->ComboIndex==0,TEXT("Third light finishes and resets chain"));
  UE_LOG(LogTemp,Display,TEXT("ACTDEMO_SMOKE_COMPLETE checks=%d failures=%d"),Checks,Failures);
  FPlatformMisc::RequestExitWithStatus(false,Failures?1:0); SetActorTickEnabled(false);
 }
}
