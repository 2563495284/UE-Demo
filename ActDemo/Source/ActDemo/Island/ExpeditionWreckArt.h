#pragma once

#include "Math/Vector.h"

class AActor;
class UMaterialInterface;
class UPrimitiveComponent;
class UStaticMesh;

namespace ExpeditionWreckArt
{
    // The coloured planks, deck and rigging are also the physical sources for local voxel removal.
    void Build(AActor& Owner, UMaterialInterface* PaintBase);
    void PlayChips(UPrimitiveComponent& Part, int32 Finish, FVector Point, FVector Normal, UStaticMesh* Splinter, UStaticMesh* Cloth);
}
