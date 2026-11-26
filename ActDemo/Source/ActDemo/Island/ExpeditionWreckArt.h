#pragma once

#include "Math/Vector.h"

class AActor;
class UMaterialInterface;
class FName;
struct FColor;
enum class EIslandChipSurface : uint8;

namespace ExpeditionWreckArt
{
    // The coloured planks, deck and rigging are also the physical sources for local voxel removal.
    void Build(AActor& Owner, UMaterialInterface* PaintBase);
    EIslandChipSurface GetChipFinish(FName PartName, int32 Finish, FColor& Paint, FColor& Alternate);
}
