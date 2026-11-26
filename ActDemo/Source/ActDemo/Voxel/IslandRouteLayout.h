#pragma once

#include "Island/IslandNeutralLayout.h"
#include "Voxel/IslandRouteFlora.h"

struct FIslandRouteSite
{
    IslandRouteFlora::EShape Kind;
    FVector Location;
    int16 Yaw;
    int32 Variant;
};

namespace IslandRouteLayout
{
    // Authority selects only final above-ground sites. Treasure positions stay on the server.
    TArray<FIslandRouteSite> Build(UWorld& World, const AVoxelTerrain& Terrain,
        const IslandNeutral::FSurface& Surface, const FIslandTreasureLayout& Treasure,
        TConstArrayView<FIslandNeutralPlacement> Neutrals);
}
