#include "Voxel/ExpeditionSurface.h"

namespace
{
    int32 TilesAcross(const FVoxelGrid& Grid) { return FMath::DivideAndRoundUp(Grid.Chunks().X, ExpeditionSurface::TileColumns); }


}

int32 ExpeditionSurface::TileCount(const FVoxelGrid& Grid)
{
    return TilesAcross(Grid) * FMath::DivideAndRoundUp(Grid.Chunks().Y, TileColumns);
}

int32 ExpeditionSurface::TileForColumn(const FVoxelGrid& Grid, int32 Column)
{
    const int32 Width = Grid.Chunks().X;
    return (Column % Width) / TileColumns + (Column / Width) / TileColumns * TilesAcross(Grid);
}

FProcMeshSection ExpeditionSurface::BuildTile(const FVoxelGrid& Grid, int32 Tile, const TSet<int32>& OmitColumns)
{
    FProcMeshSection Mesh;
    const int32 CX = Grid.Chunks().X, CY = Grid.Chunks().Y, Row = TilesAcross(Grid);
    const int32 X0 = Tile % Row * TileColumns, Y0 = Tile / Row * TileColumns;
    const auto Point = [](int32 X, int32 Y)
    {
        const int32 Shore = VoxelPlayground::ShoreDistance(EVoxelLayout::Expedition, X, Y);
        // The underwater portion tapers below the ocean. Surface terrain and edit coordinates are in cm.
        const int32 H = VoxelPlayground::BaseHeight(EVoxelLayout::Expedition, X, Y) - FMath::Clamp(Shore, 0, 300);
        return FVector(X, Y, H);
    };
    constexpr int32 Side = VoxelPlayground::Spacing * VoxelPlayground::CellsPerChunk;
    constexpr int32 SampleStep = 100;
    const int32 SamplesWide = FMath::Min(TileColumns, CX - X0) * Side / SampleStep + 1;
    const int32 SamplesHigh = FMath::Min(TileColumns, CY - Y0) * Side / SampleStep + 1;
    TArray<int32> Vertices;
    Vertices.Init(INDEX_NONE, SamplesWide * SamplesHigh);
    const auto VertexAt = [&](int32 I, int32 J)
    {
        int32& VertexIndex = Vertices[I + J * SamplesWide];
        if (VertexIndex != INDEX_NONE) return VertexIndex;
        const int32 X = Grid.Origin.X + X0 * Side + I * SampleStep;
        const int32 Y = Grid.Origin.Y + Y0 * Side + J * SampleStep;
        FProcMeshVertex Vertex;
        Vertex.Position = Point(X, Y);
        const FVector TX = Point(X+25,Y) - Point(X-25,Y), TY = Point(X,Y+25) - Point(X,Y-25);
        Vertex.Normal = FVector::CrossProduct(TX,TY).GetSafeNormal();
        Vertex.Tangent = FProcMeshTangent(TX.GetSafeNormal(), false);
        Vertex.UV0 = FVector2D(X,Y) / 100.;
        Mesh.SectionLocalBox += Vertex.Position;
        VertexIndex = Mesh.ProcVertexBuffer.Add(Vertex);
        return VertexIndex;
    };
    for (int32 CYi=Y0; CYi<FMath::Min(Y0+TileColumns,CY); ++CYi)
    {
        for (int32 CXi=X0; CXi<FMath::Min(X0+TileColumns,CX); ++CXi)
        {
            if (OmitColumns.Contains(CXi + CYi*CX)) continue;
            const int32 X = Grid.Origin.X + CXi*Side, Y = Grid.Origin.Y + CYi*Side;
            if (VoxelPlayground::ShoreDistance(EVoxelLayout::Expedition, X+Side/2,Y+Side/2) > Side) continue;
            const int32 I0 = (CXi-X0)*Side/SampleStep, J0 = (CYi-Y0)*Side/SampleStep;
            for (int32 J=J0; J<J0+Side/SampleStep; ++J)
            {
                for (int32 I=I0; I<I0+Side/SampleStep; ++I)
                {
                    const int32 A=VertexAt(I,J), B=VertexAt(I+1,J), C=VertexAt(I+1,J+1), D=VertexAt(I,J+1);
                    Mesh.ProcIndexBuffer.Append({A,C,B,A,D,C});
                }
            }
        }
    }
    Mesh.bEnableCollision = !Mesh.ProcIndexBuffer.IsEmpty();
    return Mesh;
}

void ExpeditionSurface::ExpandFirstEdit(const FVoxelDensity& Density, const FVoxelDensityPatch& Patch,
    TArray<int32>& Chunks, TSet<int32>& VisibleColumns)
{
    const FIntVector Count = Density.Grid.Chunks();
    const int32 Stride = Count.X * Count.Y;
    VisibleColumns = Density.EditedColumns();
    TSet<int32> Unique;
    for (int32 Chunk : Patch.Chunks) Unique.Add(Chunk);
    // A coarse surface covers a whole column. Its first edit needs all vertical sections,
    // including the untouched surface when the player tunnels underneath from an adjacent column.
    for (int32 Index : Patch.Chunks)
    {
        const int32 Column = Index % Stride;
        if (VisibleColumns.Contains(Column)) continue;
        VisibleColumns.Add(Column);
        for (int32 Z=0; Z<Count.Z; ++Z) Unique.Add(Column + Z*Stride);
    }
    Chunks = Unique.Array();
    Chunks.Sort();
}
