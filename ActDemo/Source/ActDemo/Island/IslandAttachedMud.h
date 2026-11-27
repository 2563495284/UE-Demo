#pragma once

#include "GameFramework/Actor.h"
#include "Voxel/VoxelDensity.h"
#include "IslandAttachedMud.generated.h"

class UIslandRouteVoxelComponent;

// One shaped throw in the local frame of the struck beam, deck or rock.
// Live collision and the render surface come from the same carved density.
UCLASS(NotPlaceable)
class ACTDEMO_API AIslandAttachedMud : public AActor
{
    GENERATED_BODY()
public:
    AIslandAttachedMud();
    static AIslandAttachedMud* Place(const FHitResult& Hit, const FVoxelEdit& Edit);
    static bool CanReceive(const UPrimitiveComponent* Surface);
    static void RefreshFor(AActor& Structure);
    static bool ContainsMud(const TArray<AIslandAttachedMud*>& Nearby, FVector WorldPoint);
    static void GatherNearby(UWorld& World, const FBox& Bounds, TArray<AIslandAttachedMud*>& Out);
    bool Strike(const FHitResult& Hit, float RadiusScale, float DepthScale);
    UIslandRouteVoxelComponent* Field() const { return Voxels; }
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;

private:
    void BeginPlay() override;
    UFUNCTION() void RebuildInitial();
    UFUNCTION() void ApplyChanges();
    void Publish();
    UPROPERTY() TObjectPtr<UIslandRouteVoxelComponent> Voxels;
    UPROPERTY(ReplicatedUsing=RebuildInitial) FVoxelEdit Stamp;
    UPROPERTY(ReplicatedUsing=ApplyChanges) TArray<uint8> Changes;
};
