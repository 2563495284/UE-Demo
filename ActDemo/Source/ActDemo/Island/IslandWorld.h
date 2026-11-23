#pragma once

#include "GameFramework/Actor.h"
#include "IslandWorld.generated.h"

class UProceduralMeshComponent;

UCLASS()
class AIslandChest : public AActor
{
    GENERATED_BODY()

public:
    AIslandChest();
    void SetPrize(bool bValue);
    bool IsPrize() const { return bPrize; }
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    UFUNCTION() void OnRep_Prize();
    UPROPERTY(ReplicatedUsing = OnRep_Prize) bool bPrize = false;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> BoxMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> PrizeMaterial;
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
