#include "Island/IslandRules.h"

#include "Voxel/VoxelDensity.h"

namespace
{
    double Cross(const FVector2D& A, const FVector2D& B)
    {
        return A.X * B.Y - A.Y * B.X;
    }

    bool FindCrossing(const FIslandTreasureLayout& Layout, FVector2D& Crossing)
    {
        const FVector2D A(Layout.Centers[0]), B(Layout.Centers[1]);
        const FVector2D AC = FVector2D(Layout.Centers[2]) - A;
        const FVector2D BD = FVector2D(Layout.Centers[3]) - B;
        const double Denominator = Cross(AC, BD);
        if (FMath::Abs(Denominator) < 1.0)
        {
            return false;
        }
        const double T = Cross(B - A, BD) / Denominator;
        const double U = Cross(B - A, AC) / Denominator;
        Crossing = A + AC * T;
        return T > 0.0 && T < 1.0 && U > 0.0 && U < 1.0;
    }

    int32 SurfaceAt(const FVector2D& Point)
    {
        return VoxelPlayground::IslandSurfaceHeight(FMath::RoundToInt(Point.X), FMath::RoundToInt(Point.Y));
    }
}

int32 FIslandClues::Count() const
{
    return FPlatformMath::CountBits(static_cast<uint32>(Mask));
}

FVector2D IslandRules::SearchCenter(int32 Index)
{
    static const FVector2D Centers[ClueCount] = {{0, -1150}, {1150, 0}, {0, 1150}, {-1150, 0}};
    return Centers[Index];
}

FLinearColor IslandRules::TeamColor(int32 Team)
{
    static const FLinearColor Colors[TeamCount] = {{.93f, .29f, .28f}, {.25f, .59f, 1.f}, {1.f, .79f, .24f}};
    return Team >= 0 && Team < TeamCount ? Colors[Team] : FLinearColor::White;
}

const TCHAR* IslandRules::TeamName(int32 Team)
{
    static const TCHAR* Names[TeamCount] = {TEXT("Red"), TEXT("Blue"), TEXT("Yellow")};
    return Team >= 0 && Team < TeamCount ? Names[Team] : TEXT("Waiting");
}

bool IslandRules::InSea(const FVector& Head)
{
    return Head.Z < 0 && VoxelPlayground::ShoreDistance(FMath::RoundToInt(Head.X), FMath::RoundToInt(Head.Y)) >= 0;
}

bool IslandRules::IsValidLayout(const FIslandTreasureLayout& Layout)
{
    FVector2D Crossing;
    if (!FindCrossing(Layout, Crossing) || !Crossing.Equals(Layout.Crossing, .01)
        || !Crossing.Equals(FVector2D(Layout.Centers[PrizeIndex]), .01))
    {
        return false;
    }
    for (int32 Index = 0; Index <= PrizeIndex; ++Index)
    {
        const FVector& Center = Layout.Centers[Index];
        const FVector2D Point(Center);
        const double Depth = SurfaceAt(Point) - Center.Z;
        const bool bPrize = Index == PrizeIndex;
        if (Depth < (bPrize ? 600 : 200) || Depth > (bPrize ? 800 : 400)
            || Center.Z - ChestExtent.Z < -700
            || (!bPrize && FVector2D::DistSquared(Point, SearchCenter(Index)) > FMath::Square(SearchRadius))
            || VoxelPlayground::ShoreDistance(FMath::RoundToInt(Center.X), FMath::RoundToInt(Center.Y)) > -550)
        {
            return false;
        }
        for (int32 Other = 0; Other < Index; ++Other)
        {
            if (FVector::DistSquared(Center, Layout.Centers[Other]) < FMath::Square(300.0))
            {
                return false;
            }
        }
    }
    return true;
}

FIslandTreasureLayout IslandRules::GenerateLayout(int32 Seed)
{
    FRandomStream Random(Seed);
    FIslandTreasureLayout Layout;
    for (int32 Attempt = 0; Attempt <= 64; ++Attempt)
    {
        const bool bFallback = Attempt == 64;
        for (int32 Index = 0; Index < ClueCount; ++Index)
        {
            FVector2D Point = SearchCenter(Index);
            const FVector2D Offset(Random.RandRange(-300, 300), Random.RandRange(-300, 300));
            if (!bFallback)
            {
                Point += Offset.GetClampedToMaxSize(SearchRadius - 1.0);
            }
            Layout.Centers[Index] = FVector(Point, SurfaceAt(Point) - (bFallback ? 300 : Random.RandRange(200, 400)));
        }
        if (FindCrossing(Layout, Layout.Crossing))
        {
            Layout.Centers[PrizeIndex] = FVector(Layout.Crossing, SurfaceAt(Layout.Crossing) - (bFallback ? 700 : Random.RandRange(600, 800)));
            if (IsValidLayout(Layout))
            {
                return Layout;
            }
        }
    }
    checkf(false, TEXT("The fixed island treasure layout must be reachable"));
    return Layout;
}
