#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "PartyPunch.h"

struct FProcMeshSection;

// Shared by the expedition island and the streamed minigame grounds.
namespace VoxelSurfaceKernel
{
    constexpr int32 Spacing = 25;
    constexpr int32 CellsPerChunk = 16;
    constexpr int32 Band = 2 * Spacing;
    inline const FIntVector FaceNeighbors[] = {{0,0,1},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1}};

    // Given a previously grounded field, only added solids and the face neighbours
    // of removed solids can belong to a newly unsupported component. IsSolid reads
    // the complete post-edit field, including samples outside the brush or chunk.
    PARTYFRAMEWORK_API void FindUnsupported(TConstArrayView<FIntVector> Seeds,
        TFunctionRef<bool(FIntVector)> IsSolid, TFunctionRef<bool(FIntVector)> IsRoot,
        TFunctionRef<void(FIntVector)> Remove);
    PARTYFRAMEWORK_API FIntVector QuantizeCenter(const FVector& Position);
    PARTYFRAMEWORK_API FIntVector QuantizeNormal(const FVector& Normal);
    // Evaluate sphere distance with integer arithmetic on every peer.
    inline int32 IntegerSqrt(int32 Value)
    {
        int32 Low = 0;
        int32 High = Value + 1;
        while (High - Low > 1)
        {
            const int32 Mid = Low + (High - Low) / 2;
            if (static_cast<int64>(Mid) * Mid <= Value)
            {
                Low = Mid;
            }
            else
            {
                High = Mid;
            }
        }
        return Low;
    }

    inline int32 ToolDistanceWithRadius(FIntVector Local,FIntVector N,int32 RadiusCm,int32 DepthCm)
    {
        const int32 Along=(Local.X*N.X+Local.Y*N.Y+Local.Z*N.Z)/1000;
        const int32 Radial=IntegerSqrt(FMath::Max(0,(Local.X*Local.X+Local.Y*Local.Y+Local.Z*Local.Z)-Along*Along));
        // A charged fist penetrates the hit face. Its clearance on the player's side stays fixed.
        return FMath::Max3(Radial-RadiusCm,-DepthCm-Along,Along-PartyPunch::LightDepthCm);
    }

    inline int32 ToolDistance(FIntVector Local, FIntVector N, bool bFill, int32 FootprintScale = 1)
    {
        const int32 Along = (Local.X * N.X + Local.Y * N.Y + Local.Z * N.Z) / 1000;
        int32 Distance;
        if (!bFill)
        {
            Distance = ToolDistanceWithRadius(Local,N,62*FootprintScale,PartyPunch::LightDepthCm);
        }
        else
        {
            const bool bWall = FMath::Abs(N.Z) < 650;
            const int32 X = Local.X - (bWall ? 30 * N.X / 1000 : 0);
            const int32 Y = Local.Y - (bWall ? 30 * N.Y / 1000 : 0);
            const int32 Z = Local.Z + (N.Z < -650 ? 40 : 0);
            const int32 Radius = IntegerSqrt(X * X + Y * Y);
            // A flat tread with a sloping skirt, trimmed at the back so a thin wall shields its far side.
            const int32 Side = Radius - (76 - FMath::Clamp(Z, 0, 40) / 3) * FootprintScale;
            Distance = FMath::Max(FMath::Max3(Side, Z - 40, -20 - Z), -16 - Along);
        }
        return Distance;
    }

    inline int16 Blend(int16 Previous, int32 Distance, bool bFill)
    {
        const int16 D = FMath::Clamp(Distance,-Band,Band);
        return bFill ? FMath::Min(Previous,D) : FMath::Max<int16>(Previous,-D);
    }

    struct FChunkSamples
    {
        static constexpr int32 Width = CellsPerChunk + 5;
        TStaticArray<int16, Width * Width * Width> Values;

        int16 Sample(int32 X, int32 Y, int32 Z) const
        {
            return Values[(X + 2) + Width * ((Y + 2) + Width * (Z + 2))];
        }

        FVector Gradient(const FIntVector& P) const
        {
            const int16* At = &Values[P.X + 2 + Width * (P.Y + 2 + Width * (P.Z + 2))];
            return FVector(At[1] - At[-1], At[Width] - At[-Width], At[Width*Width] - At[-Width*Width]);
        }
    };

    PARTYFRAMEWORK_API FProcMeshSection BuildChunk(const FChunkSamples& Samples, FVector Start);
}
