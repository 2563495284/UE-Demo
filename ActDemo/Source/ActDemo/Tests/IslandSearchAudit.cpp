#include "Island/IslandRules.h"
#include "Voxel/VoxelDensity.h"

#if !UE_BUILD_SHIPPING
bool IslandRules::AuditSearchAreas(TConstArrayView<FIslandSearchCircle> Circles, const FVoxelDensity& Density,
    TConstArrayView<FBox2D> Obstacles)
{
    constexpr int32 SeedCount = 256;
    if (Circles.Num() != ClueCount || Density.Layout != EVoxelLayout::Expedition) return false;
    FVector Guess;
    if (!FMath::SegmentIntersection2D(FVector(Circles[0].Center.X, Circles[0].Center.Y, 0.),
        FVector(Circles[2].Center.X, Circles[2].Center.Y, 0.), FVector(Circles[1].Center.X, Circles[1].Center.Y, 0.),
        FVector(Circles[3].Center.X, Circles[3].Center.Y, 0.), Guess)) return false;

    TArray<double> Misses;
    int32 PrizeQuadrants[4] = {};
    double MinOffset[4] = {MAX_dbl, MAX_dbl, MAX_dbl, MAX_dbl};
    double MaxOffset[4] = {};
    uint8 CoveredQuadrants[4] = {};
    TSet<FIntPoint> MeterCells;
    const auto Quadrant = [](FVector2D Offset) { return (Offset.X >= 0. ? 1 : 0) + (Offset.Y >= 0. ? 2 : 0); };
    for (int32 Seed = 1; Seed <= SeedCount; ++Seed)
    {
        FIslandTreasureLayout First, Replay;
        FString Reason;
        if (!GenerateLayout(Seed, 0, Circles, Density, First, &Reason, false, Obstacles)
            || !GenerateLayout(Seed, 0, Circles, Density, Replay, &Reason, false, Obstacles))
        {
            UE_LOG(LogTemp, Error, TEXT("SEARCH_AREA_AUDIT seed=%d failed=%s"), Seed, *Reason);
            return false;
        }
        FVector Exact;
        if (!FMath::SegmentIntersection2D(First.Centers[0], First.Centers[2], First.Centers[1], First.Centers[3], Exact)
            || !FVector2D(Exact).Equals(FVector2D(First.Centers[PrizeIndex]), .01)) return false;
        for (int32 Index = 0; Index <= PrizeIndex; ++Index)
            if (First.Centers[Index] != Replay.Centers[Index]) return false;
        for (int32 Index = 0; Index < ClueCount; ++Index)
        {
            const FVector2D Offset = FVector2D(First.Centers[Index]) - FVector2D(Circles[Index].Center);
            MinOffset[Index] = FMath::Min(MinOffset[Index], Offset.Size());
            MaxOffset[Index] = FMath::Max(MaxOffset[Index], Offset.Size());
            CoveredQuadrants[Index] |= 1 << Quadrant(Offset);
        }
        const FVector2D Delta = First.Crossing - FVector2D(Guess);
        Misses.Add(Delta.Size());
        ++PrizeQuadrants[Quadrant(Delta)];
        MeterCells.Add({FMath::FloorToInt(Delta.X / 100.), FMath::FloorToInt(Delta.Y / 100.)});
    }

    Misses.Sort();
    bool bPass = MeterCells.Num() >= SeedCount / 2;
    for (int32 Index = 0; Index < ClueCount; ++Index)
    {
        bPass &= Circles[Index].Radius == ExpeditionSearchRadius && CoveredQuadrants[Index] == 15 && PrizeQuadrants[Index] > 0;
        UE_LOG(LogTemp, Display, TEXT("SEARCH_AREA_CIRCLE clue=%c radius_cm=%d min_offset_cm=%.0f max_offset_cm=%.0f quadrants=%u"),
            'A' + Index, Circles[Index].Radius, MinOffset[Index], MaxOffset[Index], CoveredQuadrants[Index]);
    }
    bPass &= Misses[0] >= 1000.;
    UE_LOG(LogTemp, Display, TEXT("SEARCH_AREA_AUDIT pass=%d seeds=%d replay=%d exact=%d obstacles=%d center_guess_miss_cm=%.0f/%.0f/%.0f unique_meter_cells=%d prize_quadrants=%d/%d/%d/%d"),
        bPass, SeedCount, SeedCount, SeedCount, Obstacles.Num(), Misses[0], Misses[SeedCount / 2], Misses.Last(), MeterCells.Num(),
        PrizeQuadrants[0], PrizeQuadrants[1], PrizeQuadrants[2], PrizeQuadrants[3]);
    return bPass;
}
#endif
