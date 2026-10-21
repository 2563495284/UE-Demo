#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Character/ActCharacter.h"
#include "ActGameMode.generated.h"
class UBoxComponent;
UENUM(BlueprintType)
enum class EActOutcome : uint8 { Playing, Defeat, Victory };
DECLARE_MULTICAST_DELEGATE(FActRunChanged);

UCLASS(Blueprintable)
class ACTDEMO_API AActSpawnPoint : public AActor
{
 GENERATED_BODY()
public:
 AActSpawnPoint();
 UPROPERTY(EditAnywhere, BlueprintReadWrite) EActEnemyKind Kind = EActEnemyKind::Scout;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Encounter = 0;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UActCharacterProfile> Profile;
};
UCLASS(Blueprintable)
class ACTDEMO_API AActCheckpoint : public AActor
{
 GENERATED_BODY()
public:
 AActCheckpoint();
 UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Trigger;
 UFUNCTION() void Enter(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComponent, int32 BodyIndex, bool bSweep, const FHitResult& Hit);
};
UCLASS()
class ACTDEMO_API AActGameMode : public AGameModeBase
{
 GENERATED_BODY()
public:
 AActGameMode();
 virtual void BeginPlay() override;
 virtual void Tick(float Dt) override;
 void CharacterDied(AActCharacter* Character);
 UFUNCTION(BlueprintCallable) void ResetRun(bool bFromStart);
 void ActivateCheckpoint(FVector Location);
 UPROPERTY(BlueprintReadOnly) EActOutcome Outcome = EActOutcome::Playing;
 UPROPERTY(BlueprintReadOnly) TObjectPtr<AActCharacter> Boss;
 UPROPERTY(BlueprintReadOnly) bool bCheckpoint = false;
 UPROPERTY(BlueprintReadOnly) int32 EnemiesDefeated = 0;
 UPROPERTY(BlueprintReadOnly) FText Objective;
 FActRunChanged OnRunChanged;
private:
 void SpawnEncounters();
 UPROPERTY() TArray<TObjectPtr<AActCharacter>> Enemies;
 FVector CheckpointLocation;
 FVector StartLocation = FVector(-2500,0,100);
};
