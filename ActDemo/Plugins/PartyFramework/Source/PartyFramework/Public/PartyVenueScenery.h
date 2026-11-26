#pragma once

#include "CoreMinimal.h"

namespace PartyVenueScenery
{
    inline const FName ShelfTag(TEXT("PartyVenueShelf"));
    inline const FName WaterTag(TEXT("PartyVenueWater"));
    inline const FName StructureTag(TEXT("PartyVenueStructure"));

    inline FName GroupTag(int32 Id)
    {
        return FName(StructureTag, Id + 1);
    }
}
