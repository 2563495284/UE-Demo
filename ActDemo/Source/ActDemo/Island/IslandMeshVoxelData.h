#pragma once

#include "ProceduralMeshComponent.h"

struct FIslandVoxelTri
{
    FProcMeshVertex V[3];
    int32 Material = 0;
};

// Source triangles carry the painted boundary. Closed spans fill the interior of each
// voxel column. Both survive in the grid until the corresponding cell is removed.
struct FIslandMeshVoxelData
{
    TArray<FIslandVoxelTri> Source;
    TArray<FIslandVoxelTri> Skin;
    TArray<int32> SkinCells;
    TArray<int32> CellPaint;
    TArray<int32> FaceStarts;
    FBox Box = FBox(ForceInit);
    FVector Step;
    FIntVector Size;
    TBitArray<> Filled, Missing;
    int32 Total = 0, Remaining = 0;
    static TSharedRef<FIslandMeshVoxelData> Create();
    void Build(FVector WorldScale);
    void Mesh(TArray<FProcMeshSection>& Sections) const;
    int32 Index(FIntVector P) const { return P.X + Size.X*(P.Y + Size.Y*P.Z); }
    FIntVector XYZ(int32 I) const { return {I%Size.X,(I/Size.X)%Size.Y,I/(Size.X*Size.Y)}; }
    bool Inside(FIntVector P) const { return P.GetMin()>=0 && P.X<Size.X && P.Y<Size.Y && P.Z<Size.Z; }
    bool Live(int32 I) const { return Filled.IsValidIndex(I) && Filled[I] && !Missing[I]; }
    FIntVector Cell(FVector P) const;
    FVector Center(int32 I) const { return Box.Min+(FVector(XYZ(I))+FVector(.5))*Step; }
    void VisitBox(FBox Local,TFunctionRef<void(int32)> Visit) const;
};

bool ReadIslandVoxelSource(UPrimitiveComponent& Source,TArray<FIslandVoxelTri>& Triangles);
bool ReadIslandVoxelRenderSource(const UStaticMesh& Asset, TArray<FIslandVoxelTri>& Triangles);
