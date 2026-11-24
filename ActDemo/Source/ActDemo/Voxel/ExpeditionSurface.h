#pragma once

#include "Voxel/VoxelDensity.h"
#include "ProceduralMeshComponent.h"

namespace ExpeditionSurface
{
    inline constexpr int32 TileColumns = 4;
    int32 TileCount(const FVoxelGrid& Grid);
    int32 TileForColumn(const FVoxelGrid& Grid, int32 Column);
    FProcMeshSection BuildTile(const FVoxelGrid& Grid, int32 Tile, const TSet<int32>& OmitColumns);
    void ExpandFirstEdit(const FVoxelDensity& Density, const FVoxelDensityPatch& Patch, TArray<int32>& Chunks, TSet<int32>& VisibleColumns);
}
