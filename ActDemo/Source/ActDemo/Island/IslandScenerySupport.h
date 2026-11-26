#pragma once

#include "CoreMinimal.h"

class UIslandMeshVoxels;
class AActor;
class AVoxelTerrain;
struct FVoxelDensity;

namespace IslandScenerySupport
{
    using FCellGroup = TMap<UIslandMeshVoxels*,TArray<int32>>;

    // Collect disconnected material after a cut across a group of touching bodies.
    // The owning authority applies and publishes all returned removals together.
    ACTDEMO_API void FindUnsupported(const FVoxelDensity& Ground,
        TConstArrayView<UIslandMeshVoxels*> Parts, TMap<UIslandMeshVoxels*,TArray<int32>>& Removed,
        TFunctionRef<bool(const UIslandMeshVoxels&,int32,FVector)> ExtraAnchor, TArray<FCellGroup>* Groups = nullptr);
    ACTDEMO_API void FindUnsupported(const FVoxelDensity& Ground,
        TConstArrayView<UIslandMeshVoxels*> Parts, TMap<UIslandMeshVoxels*,TArray<int32>>& Removed, TArray<FCellGroup>* Groups = nullptr);
    ACTDEMO_API void InspectActor(AActor& Actor, const AVoxelTerrain& Terrain,
        TMap<UIslandMeshVoxels*,TArray<int32>>& Removed, TArray<FCellGroup>* Groups = nullptr);
}
