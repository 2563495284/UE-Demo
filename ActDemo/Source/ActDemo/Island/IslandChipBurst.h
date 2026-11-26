#pragma once

#include "GameFramework/Actor.h"
#include "IslandChipBurst.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

// One local burst draws the large chips and smaller flecks from a single hit.
UCLASS(Transient, NotPlaceable)
class AIslandChipBurst : public AActor
{
    GENERATED_BODY()
public:
    AIslandChipBurst();
    void Launch(FVector Normal, UStaticMesh& Shape, FVector Scale, UMaterialInterface* Finish,
        UMaterialInterface* Alternate, bool bLightScraps);
    void Tick(float DeltaSeconds) override;

private:
    struct FChip
    {
        FVector Start, Velocity, SpinAxis;
        FTransform Transform;
        double SpinRate, Lifetime;
    };
    struct FBatch
    {
        TArray<FChip> Chips;
        TArray<FTransform> Poses;
    };
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Chunks[2];
    FBatch Batches[2];
    FVector Outward = FVector::UpVector;
    double Age = 0., Gravity = 870.;
#if !UE_BUILD_SHIPPING
    bool bMeasureVisibility = false;
#endif
};
