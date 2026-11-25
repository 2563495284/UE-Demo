#include "VoxelSurfaceKernel.h"

#include "Generators/MarchingCubes.h"
#include "ProceduralMeshComponent.h"

namespace VoxelSurfaceKernel
{
    // Use GeometryCore's topology with direct edge indices for our fixed, dense 16-cell chunks.
    // Adjacent cells share the same edge slot, including when their corner order is reversed.
    struct FChunkTopology : UE::Geometry::FMarchingCubes
    {
        using FMarchingCubes::EdgeIndices;
        using FMarchingCubes::TriTable;
    };

}

void VoxelSurfaceKernel::FindUnsupported(TConstArrayView<FIntVector> Seeds,
    TFunctionRef<bool(FIntVector)> IsSolid, TFunctionRef<bool(FIntVector)> IsRoot,
    TFunctionRef<void(FIntVector)> Remove)
{
    TSet<FIntVector> Grounded;
    TSet<FIntVector> Component;
    TArray<FIntVector> Pending;
    for (FIntVector Seed : Seeds)
    {
        if (Grounded.Contains(Seed) || !IsSolid(Seed)) continue;
        Component.Reset();
        Pending.Reset();
        Component.Add(Seed);
        Pending.Add(Seed);
        bool bGrounded = false;
        while (!Pending.IsEmpty() && !bGrounded)
        {
            const FIntVector Point = Pending.Pop(EAllowShrinking::No);
            if (IsRoot(Point))
            {
                bGrounded = true;
                break;
            }
            // Visit down first on the stack, so a cut in ordinary ground reaches
            // bedrock without walking across the rest of the island.
            for (FIntVector Offset : FaceNeighbors)
            {
                const FIntVector Next = Point + Offset;
                if (Grounded.Contains(Next))
                {
                    bGrounded = true;
                    break;
                }
                if (!Component.Contains(Next) && IsSolid(Next))
                {
                    Component.Add(Next);
                    Pending.Add(Next);
                }
            }
        }
        if (bGrounded) Grounded.Append(Component);
        else for (FIntVector Point : Component) Remove(Point);
    }
}

FIntVector VoxelSurfaceKernel::QuantizeNormal(const FVector& Normal)
{
    const FVector Scaled = Normal.GetSafeNormal() * 1000.;
    return FIntVector(FMath::RoundToInt(Scaled.X), FMath::RoundToInt(Scaled.Y), FMath::RoundToInt(Scaled.Z));
}

FIntVector VoxelSurfaceKernel::QuantizeCenter(const FVector& Position)
{
    constexpr int32 CenterStep = 5;
    return FIntVector(FMath::RoundToInt(Position.X / CenterStep), FMath::RoundToInt(Position.Y / CenterStep),
        FMath::RoundToInt(Position.Z / CenterStep)) * CenterStep;
}

FProcMeshSection VoxelSurfaceKernel::BuildChunk(const FChunkSamples& Samples, FVector Start)
{
    constexpr int32 Side = CellsPerChunk;
    constexpr int32 Width = FChunkSamples::Width;
    constexpr int32 EdgeWidth = Side + 1;
    static const FIntVector Corners[] = {{0,0,0},{1,0,0},{1,0,1},{0,0,1},{0,1,0},{1,1,0},{1,1,1},{0,1,1}};
    static constexpr int32 CornerOffsets[] = {0,1, 1+Width*Width,Width*Width, Width,Width+1,Width+1+Width*Width,Width+Width*Width};
    TStaticArray<uint16, EdgeWidth * EdgeWidth * EdgeWidth * 3> EdgeVertices;
    FMemory::Memset(EdgeVertices.GetData(), 0xff, sizeof(EdgeVertices));
    uint32 Inside[EdgeWidth][EdgeWidth];
    for (int32 Z = 0; Z < EdgeWidth; ++Z)
        for (int32 Y = 0; Y < EdgeWidth; ++Y)
        {
            uint32 Bits = 0;
            for (int32 X = 0; X < EdgeWidth; ++X) Bits |= uint32(Samples.Sample(X,Y,Z) < 0) << X;
            Inside[Z][Y] = Bits;
        }
    FProcMeshSection Section;
    for (int32 Z = 0; Z < Side; ++Z)
    {
        for (int32 Y = 0; Y < Side; ++Y)
        {
            // Eight equal corner signs cannot emit a triangle. Skip whole spans of air or solid rock.
            const uint32 AnySolid = Inside[Z][Y] | Inside[Z+1][Y] | Inside[Z][Y+1] | Inside[Z+1][Y+1];
            const uint32 AllSolid = Inside[Z][Y] & Inside[Z+1][Y] & Inside[Z][Y+1] & Inside[Z+1][Y+1];
            uint32 Crossings = (AnySolid | (AnySolid >> 1)) & ~(AllSolid & (AllSolid >> 1)) & ((1u << Side) - 1);
            while (Crossings)
            {
                const int32 X = FMath::CountTrailingZeros(Crossings);
                Crossings &= Crossings - 1;
                const int16* Cell = &Samples.Values[X + 2 + Width * (Y + 2 + Width * (Z + 2))];
                int32 Mask = 0;
                for (int32 C = 0; C < 8; ++C) Mask |= int32(Cell[CornerOffsets[C]] < 0) << C;
                const FIntVector Origin(X,Y,Z);
                const auto GetVertex = [&](int32 Edge) -> int32
                {
                    const int32 A = FChunkTopology::EdgeIndices[Edge][0], B = FChunkTopology::EdgeIndices[Edge][1];
                    const FIntVector Delta = Corners[B] - Corners[A];
                    const FIntVector Lower = Origin + Corners[A].ComponentMin(Corners[B]);
                    const int32 Axis = Delta.X != 0 ? 0 : (Delta.Y != 0 ? 1 : 2);
                    uint16& Cached = EdgeVertices[Axis + 3 * (Lower.X + EdgeWidth * (Lower.Y + EdgeWidth * Lower.Z))];
                    if (Cached != MAX_uint16) return Cached;
                    const double VA = Cell[CornerOffsets[A]] == 0 ? .01 : double(Cell[CornerOffsets[A]]);
                    const double VB = Cell[CornerOffsets[B]] == 0 ? .01 : double(Cell[CornerOffsets[B]]);
                    const double Alpha = VA / (VA - VB);
                    const FVector Local = FVector(Origin + Corners[A]) + FVector(Delta) * Alpha;
                    Cached = Section.ProcVertexBuffer.Num();
                    FProcMeshVertex& Vertex = Section.ProcVertexBuffer.AddDefaulted_GetRef();
                    Vertex.Position = Start + Local * Spacing;
                    // A marching-cubes vertex lies on a grid edge, so its trilinear gradient
                    // is the linear interpolation of the two endpoint gradients.
                    Vertex.Normal = FMath::Lerp(Samples.Gradient(Origin + Corners[A]), Samples.Gradient(Origin + Corners[B]), Alpha)
                        .GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
                    FVector Tangent, Bitangent;
                    Vertex.Normal.FindBestAxisVectors(Tangent, Bitangent);
                    Vertex.Tangent = FProcMeshTangent(Tangent, false);
                    Vertex.UV0 = FVector2D(Vertex.Position) / 100.;
                    Section.SectionLocalBox += Vertex.Position;
                    return Cached;
                };
                for (uint64 Triangles = FChunkTopology::TriTable[Mask]; Triangles; Triangles >>= 12)
                {
                    const int32 A = GetVertex(Triangles & 15);
                    const int32 B = GetVertex((Triangles >> 4) & 15);
                    const int32 C = GetVertex((Triangles >> 8) & 15);
                    const auto& VA = Section.ProcVertexBuffer[A];
                    const auto& VB = Section.ProcVertexBuffer[B];
                    const auto& VC = Section.ProcVertexBuffer[C];
                    const FVector Face = FVector::CrossProduct(VB.Position - VA.Position, VC.Position - VA.Position);
                    if (Face.SizeSquared() <= UE_SMALL_NUMBER) continue;
                    const bool bReverse = FVector::DotProduct(Face, VA.Normal + VB.Normal + VC.Normal) > 0.;
                    Section.ProcIndexBuffer.Append({A, bReverse ? C : B, bReverse ? B : C});
                }
            }
        }
    }
    Section.bEnableCollision = !Section.ProcIndexBuffer.IsEmpty();
    return Section;
}
