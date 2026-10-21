#include "World/ActGameMode.h"
#include "Character/ActPlayerController.h"
#include "AI/ActAIController.h"
#include "Combat/ActAttributeSet.h"
#include "Tests/ActSmokeProbe.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"

AActSpawnPoint::AActSpawnPoint()
{ RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root")); bIsEditorOnlyActor = false; }
AActCheckpoint::AActCheckpoint()
{
 Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger")); RootComponent = Trigger;
 Trigger->SetBoxExtent(FVector(180,180,160)); Trigger->SetCollisionProfileName(TEXT("Trigger"));
 Trigger->OnComponentBeginOverlap.AddDynamic(this, &AActCheckpoint::Enter);
}
void AActCheckpoint::Enter(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
 const auto* Character = Cast<AActCharacter>(Other);
 if (Character && Character->Faction == EActFaction::Player && Character->IsAlive())
  if (auto* Game = GetWorld()->GetAuthGameMode<AActGameMode>()) Game->ActivateCheckpoint(GetActorLocation());
}
AActGameMode::AActGameMode()
{
 DefaultPawnClass = AActCharacter::StaticClass(); PlayerControllerClass = AActPlayerController::StaticClass();
 PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.TickInterval = .5f;
}
void AActGameMode::BeginPlay()
{
 Super::BeginPlay();
 if (TActorIterator<APlayerStart> It(GetWorld()); It) StartLocation = It->GetActorLocation();
 Objective = FText::FromString(TEXT("Cross the training court. Find the amber beacon."));
 SpawnEncounters();
#if !UE_BUILD_SHIPPING
 if (FParse::Param(FCommandLine::Get(), TEXT("ActDemoSmoke"))) GetWorld()->SpawnActor<AActSmokeProbe>();
#endif
}
void AActGameMode::SpawnEncounters()
{
 Boss = nullptr;
 for (TActorIterator<AActSpawnPoint> It(GetWorld()); It; ++It)
 {
  if (bCheckpoint && It->Encounter == 0 && It->Kind != EActEnemyKind::Dummy) continue;
  FTransform Transform(It->GetActorRotation(), It->GetActorLocation());
  auto* Enemy = GetWorld()->SpawnActorDeferred<AActCharacter>(AActCharacter::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
  Enemy->Faction = It->Kind == EActEnemyKind::Dummy ? EActFaction::Training : EActFaction::Enemy;
  Enemy->EnemyKind = It->Kind;
  Enemy->Profile = It->Profile;
  Enemy->AutoPossessAI = It->Kind == EActEnemyKind::Dummy ? EAutoPossessAI::Disabled : EAutoPossessAI::PlacedInWorldOrSpawned;
  switch (It->Kind)
  {
   case EActEnemyKind::Scout: Enemy->InitialHealth=100; Enemy->DamageScale=.65f; Enemy->DisplayName=FText::FromString(TEXT("ASH SCOUT")); break;
   case EActEnemyKind::Brute: Enemy->InitialHealth=200; Enemy->DamageScale=.85f; Enemy->DisplayName=FText::FromString(TEXT("IRON GUARD")); break;
   case EActEnemyKind::Boss: Enemy->InitialHealth=850; Enemy->DamageScale=1.f; Enemy->DisplayName=FText::FromString(TEXT("WARDEN OF ASH")); Enemy->SetActorScale3D(FVector(1.2f)); Boss=Enemy; break;
   case EActEnemyKind::Dummy: Enemy->InitialHealth=10000; Enemy->DamageScale=0; Enemy->DisplayName=FText::FromString(TEXT("TRAINING EFFIGY")); break;
  }
  Enemy->FinishSpawning(Transform);
  Enemies.Add(Enemy);
 }
 OnRunChanged.Broadcast();
}
void AActGameMode::CharacterDied(AActCharacter* Character)
{
 if (Outcome != EActOutcome::Playing) return;
 if (Character->Faction == EActFaction::Player) Outcome = EActOutcome::Defeat;
 else if (Character == Boss) Outcome = EActOutcome::Victory;
 else if (Character->Faction == EActFaction::Enemy) ++EnemiesDefeated;
 if (Outcome != EActOutcome::Playing)
 {
  for (AActCharacter* Enemy : Enemies) if (IsValid(Enemy)) if (auto* AI=Cast<AActAIController>(Enemy->GetController())) AI->StopCombat();
  if (auto* PC=Cast<AActPlayerController>(UGameplayStatics::GetPlayerController(this,0))) PC->ShowOutcome();
 }
 OnRunChanged.Broadcast();
}
void AActGameMode::ResetRun(bool bFromStart)
{
 Outcome = EActOutcome::Playing;
 if (bFromStart) { bCheckpoint=false; EnemiesDefeated=0; }
 for (AActCharacter* Enemy : Enemies)
  if (IsValid(Enemy)) { auto* AI=Enemy->GetController(); Enemy->Destroy(); if (AI) AI->Destroy(); }
 Enemies.Reset(); Boss=nullptr;
 auto* PC=UGameplayStatics::GetPlayerController(this,0);
 if (PC)
 {
  if (APawn* Pawn=PC->GetPawn()) { PC->UnPossess(); Pawn->Destroy(); }
  RestartPlayerAtTransform(PC, FTransform(FRotator::ZeroRotator, bCheckpoint ? CheckpointLocation : StartLocation));
  PC->SetControlRotation(FRotator(-15,0,0));
 }
 Objective = FText::FromString(bCheckpoint ? TEXT("The beacon holds. Defeat the Warden beyond the gate.") : TEXT("Cross the training court. Find the amber beacon."));
 SpawnEncounters(); OnRunChanged.Broadcast();
}
void AActGameMode::ActivateCheckpoint(FVector Location)
{
 if (bCheckpoint || Outcome != EActOutcome::Playing) return;
 bCheckpoint=true; CheckpointLocation=Location+FVector(-220,0,100);
 if (auto* Hero=Cast<AActCharacter>(UGameplayStatics::GetPlayerPawn(this,0))) Hero->RestoreAtCheckpoint();
 Objective=FText::FromString(TEXT("BEACON LIT  /  Health and flasks restored. Face the Warden."));
 OnRunChanged.Broadcast();
}
void AActGameMode::Tick(float Dt)
{
 Super::Tick(Dt);
 if (Outcome != EActOutcome::Playing) return;
 if (auto* Hero=Cast<AActCharacter>(UGameplayStatics::GetPlayerPawn(this,0)))
 {
  if (Hero->IsAlive() && Hero->GetActorLocation().Z < -600.f) Hero->ApplyResource(UActHealthEffect::StaticClass(), -10000.f);
 }
}
