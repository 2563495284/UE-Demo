#include "Island/IslandRules.h"
#include "Voxel/VoxelDensity.h"

#if !UE_BUILD_SHIPPING
namespace SearchAreaAudit
{
    // Exercise the shipped shallow brush on a copy of the authored island. Each target is
    // acquired at the current top surface, so no edit can start at a hidden box underground.
    bool Excavate(FVoxelDensity& Field, const FVector& Center)
    {
        const FBox Bounds = FBox::BuildAABB(Center,IslandRules::ChestExtent);
        const FVector Min = (Bounds.Min-FVector(Field.Grid.Origin))/VoxelPlayground::Spacing;
        const FVector Max = (Bounds.Max-FVector(Field.Grid.Origin))/VoxelPlayground::Spacing;
        FVoxelEdit Cut;
        Cut.Action = EVoxelAction::Punch;
        Cut.BrushId = VoxelPlayground::ShallowCut;
        for (int32 X=FMath::FloorToInt(Min.X); X<=FMath::CeilToInt(Max.X); ++X)
            for (int32 Y=FMath::FloorToInt(Min.Y); Y<=FMath::CeilToInt(Max.Y); ++Y)
            {
                const FVector2D XY(Field.Grid.Origin.X+X*VoxelPlayground::Spacing,Field.Grid.Origin.Y+Y*VoxelPlayground::Spacing);
                for (int32 Stroke=0; Stroke<16; ++Stroke)
                {
                    const auto Height = Field.SurfaceAt(XY);
                    if (!Height || Height.GetValue()<Bounds.Min.Z-VoxelPlayground::Spacing) break;
                    Cut.Center = VoxelPlayground::QuantizeCenter(FVector(XY,Height.GetValue()));
                    if (!Field.Grid.IsValidEdit(Cut)) return false;
                    const FVoxelDensityPatch Patch = Field.PrepareEdit(Cut);
                    if (Patch.Samples.IsEmpty()) break;
                    Field.Apply(Patch);
                }
            }
        return Field.IsVolumeClear(Bounds);
    }
}

bool IslandRules::AuditSearchAreas(int32 DecoyCount, TConstArrayView<FIslandSearchCircle> Circles, const FVoxelDensity& Density,
    TConstArrayView<FBox2D> Obstacles, const TFunction<bool(FVector2D)>& CanReachDecoy)
{
    constexpr int32 SeedCount = 256;
    if (Circles.Num() != ClueCount || Density.Layout != EVoxelLayout::Expedition) return false;
    FVector Guess;
    if (!FMath::SegmentIntersection2D(FVector(Circles[0].Center.X, Circles[0].Center.Y, 0.),
        FVector(Circles[2].Center.X, Circles[2].Center.Y, 0.), FVector(Circles[1].Center.X, Circles[1].Center.Y, 0.),
        FVector(Circles[3].Center.X, Circles[3].Center.Y, 0.), Guess)) return false;

    int32 TrapReplayCount = 0, DigCount = 0;
    uint8 ExcavatedRegions = 0;
    TArray<double> Misses;
    int32 PrizeQuadrants[4] = {};
    double MinOffset[4] = {MAX_dbl, MAX_dbl, MAX_dbl, MAX_dbl};
    double MaxOffset[4] = {};
    uint8 CoveredQuadrants[4] = {};
    TSet<FIntPoint> MeterCells;
    const auto Quadrant = [](FVector2D Offset) { return (Offset.X >= 0. ? 1 : 0) + (Offset.Y >= 0. ? 2 : 0); };
    for (int32 Seed = 1; Seed <= SeedCount; ++Seed)
    {
        FIslandTreasureLayout First, Replay, NoTraps;
        FString Reason;
        if (!GenerateLayout(Seed, DecoyCount, Circles, Density, First, &Reason, false, Obstacles, CanReachDecoy)
            || !GenerateLayout(Seed, DecoyCount, Circles, Density, Replay, &Reason, false, Obstacles, CanReachDecoy)
            || !GenerateLayout(Seed, 0, Circles, Density, NoTraps, &Reason, false, Obstacles, CanReachDecoy))
        {
            UE_LOG(LogTemp, Error, TEXT("SEARCH_AREA_AUDIT seed=%d failed=%s"), Seed, *Reason);
            return false;
        }
        FVector Exact;
        if (!FMath::SegmentIntersection2D(First.Centers[0], First.Centers[2], First.Centers[1], First.Centers[3], Exact)
            || !FVector2D(Exact).Equals(FVector2D(First.Centers[PrizeIndex]), .01)) return false;
        for (int32 Index = 0; Index <= PrizeIndex; ++Index)
            if (First.Centers[Index] != Replay.Centers[Index] || First.Centers[Index] != NoTraps.Centers[Index]) return false;
        if (First.Decoys.Num()!=DecoyCount || Replay.Decoys.Num()!=DecoyCount) return false;
        for (int32 Index=0; Index<DecoyCount; ++Index)
        {
            if (First.Decoys[Index].Center!=Replay.Decoys[Index].Center
                || First.Decoys[Index].ClueIndex!=Replay.Decoys[Index].ClueIndex) return false;
            ++TrapReplayCount;
        }
        if (Seed==1 && DecoyCount>0)
        {
            FVoxelDensity Excavation(Density);
            for (const auto& Trap : First.Decoys)
            {
                if (!SearchAreaAudit::Excavate(Excavation,Trap.Center))
                {
                    UE_LOG(LogTemp,Error,TEXT("SEARCH_TRAP_AUDIT inaccessible=%s area=%d"),*Trap.Center.ToCompactString(),Trap.ClueIndex);
                    return false;
                }
                ++DigCount;
                ExcavatedRegions |= 1 << Trap.ClueIndex;
            }
            for (const FVector& Real : First.Centers)
                if (!Excavation.IsVolumeSolid(FBox::BuildAABB(Real,ChestExtent))) return false;
        }
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
    bPass &= DigCount==DecoyCount && (DecoyCount<ClueCount || ExcavatedRegions==15);
    UE_LOG(LogTemp,Display,TEXT("SEARCH_TRAP_AUDIT per_seed=%d replayed=%d unchanged_true_layouts=%d surface_excavations=%d regions=%d"),
        DecoyCount,TrapReplayCount,SeedCount,DigCount,ExcavatedRegions);
    UE_LOG(LogTemp, Display, TEXT("SEARCH_AREA_AUDIT pass=%d seeds=%d replay=%d exact=%d obstacles=%d center_guess_miss_cm=%.0f/%.0f/%.0f unique_meter_cells=%d prize_quadrants=%d/%d/%d/%d"),
        bPass, SeedCount, SeedCount, SeedCount, Obstacles.Num(), Misses[0], Misses[SeedCount / 2], Misses.Last(), MeterCells.Num(),
        PrizeQuadrants[0], PrizeQuadrants[1], PrizeQuadrants[2], PrizeQuadrants[3]);
    return bPass;
}
#endif
