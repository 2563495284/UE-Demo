#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
struct FIslandVoxelTri;

// Cooked NullRHI worlds discard the engine render LOD. Both peers read these exact painted triangles.
PARTYFRAMEWORK_API bool ReadIslandVoxelArchive(const UStaticMesh& Asset, TArray<FIslandVoxelTri>& Out);

#if WITH_EDITOR
PARTYFRAMEWORK_API bool WriteIslandVoxelSourceArchive(TArray<UStaticMesh*> Assets);
#endif
