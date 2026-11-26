#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
struct FIslandVoxelTri;

// Cooked NullRHI worlds discard the engine render LOD. Both peers read these exact painted triangles.
bool ReadIslandVoxelArchive(const UStaticMesh& Asset, TArray<FIslandVoxelTri>& Out);

#if WITH_EDITOR
ACTDEMO_API bool WriteIslandVoxelSourceArchive(TArray<UStaticMesh*> Assets);
#endif
