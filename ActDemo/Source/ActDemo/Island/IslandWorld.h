#pragma once

#include "GameFramework/Actor.h"
#include "IslandWorld.generated.h"

class UProceduralMeshComponent;
class AVoxelCharacter;

UCLASS()
class AIslandChest : public AActor
{
    GENERATED_BODY()

public:
    AIslandChest();
    void SetPrize(bool bValue);
    bool IsPrize() const { return bPrize; }
    void MarkChallengeResolved();
    bool IsChallengeResolved() const { return bChallengeResolved; }
    bool IsReachableFrom(const AVoxelCharacter& Hero) const;
    void AnimateLock(double FinishAt, bool bAssisted);
    void RevealPrize(double ServerTime);
    bool IsPrizeRevealed() const { return PrizeOpenedAt >= 0.; }
    void Tick(float DeltaSeconds) override;
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    UFUNCTION() void OnRep_Prize();
    UFUNCTION() void OnRep_ChallengeResolved();
    UFUNCTION() void OnRep_PrizeOpened();
    UPROPERTY(ReplicatedUsing = OnRep_Prize) bool bPrize = false;
    UPROPERTY(ReplicatedUsing = OnRep_ChallengeResolved) bool bChallengeResolved = false;
    UPROPERTY(ReplicatedUsing = OnRep_PrizeOpened) double PrizeOpenedAt = -1.;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> BoxMesh;
    UPROPERTY() TObjectPtr<USceneComponent> LidHinge;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> LidMesh;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> ClaspMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> PrizeMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> ClueMaterial;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> Compass;
    UPROPERTY() TObjectPtr<UStaticMesh> CoinShape;
    UPROPERTY() TObjectPtr<UStaticMesh> NeedleShape;
    double LocalFinishAt = 0.0;
    double LocalLockDuration = 0.0;
    double LidAngle = 0.0;
};

UCLASS()
class ACTDEMO_API AIslandWater : public AActor
{
    GENERATED_BODY()

public:
    AIslandWater();
    UPROPERTY(EditAnywhere) TObjectPtr<UMaterialInterface> WaterMaterial;

protected:
    void BeginPlay() override;

private:
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> WaterMesh;
};
