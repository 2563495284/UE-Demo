#pragma once

#include "GameFramework/Actor.h"
#include "IslandWorld.generated.h"

class UProceduralMeshComponent;
class AVoxelCharacter;
class AVoxelTerrain;
struct FVoxelDensity;

UCLASS()
class AIslandChest : public AActor
{
    GENERATED_BODY()

public:
    AIslandChest();
    static bool IsLidExposed(const FVoxelDensity& Soil, FVector BuriedCenter);
    void SeatAtReveal(const AVoxelTerrain& Terrain);
    double GetInteractionDistanceSquared(FVector Origin) const;
    FVector GetDiscoveryPoint() const { return GetActorLocation() + FVector(0., 0., DiscoveryRise); }
    bool HasDiscoveryTarget() const;
    void SetPrize(bool bValue);
    bool IsPrize() const { return bPrize; }
    void MarkChallengeResolved();
    bool IsChallengeResolved() const { return bChallengeResolved; }
    bool IsReachableFrom(const AVoxelCharacter& Hero, FVector* VisiblePoint = nullptr) const;
    bool FindInteractionPoint(FVector Eye, const AVoxelCharacter& Hero, FVector* VisiblePoint) const;
    void AnimateLock(double FinishAt, bool bAssisted);
    void RevealPrize(double ServerTime);
    bool IsPrizeRevealed() const { return PrizeOpenedAt >= 0.; }
    void BeginPlay() override;
    void Tick(float DeltaSeconds) override;
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    UPROPERTY(Replicated) double DiscoveryRise = 0.;
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
    UPROPERTY() TObjectPtr<UMaterialInterface> CompassFace;
    UPROPERTY(Transient) TObjectPtr<USceneComponent> Compass;
    UPROPERTY() TObjectPtr<UMaterialInterface> GildedLacquer;
    UPROPERTY() TObjectPtr<UStaticMesh> DialShape;
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
    UPROPERTY() TObjectPtr<UMaterialInterface> LagoonMaterial;
};
