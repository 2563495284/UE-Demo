#include "Voxel/VoxelDensity.h"
#include "VoxelSurfaceKernel.h"

namespace
{
    using namespace VoxelPlayground;
    using VoxelSurfaceKernel::IntegerSqrt;

    // The outside border is air. The islands have one additional zero-distance
    // sample at -775 cm before their first solid layer at -750 cm.
    int32 FoundationLayer(EVoxelLayout Layout)
    {
        return Layout == EVoxelLayout::Playground ? 1 : 2;
    }

    int32 LengthSquared(const FIntVector& Vector)
    {
        return Vector.X * Vector.X + Vector.Y * Vector.Y + Vector.Z * Vector.Z;
    }

    int32 BoxDistance(const FIntVector& Point, const FIntVector& Center, const FIntVector& HalfExtent)
    {
        const FIntVector Delta(FMath::Abs(Point.X - Center.X) - HalfExtent.X,
            FMath::Abs(Point.Y - Center.Y) - HalfExtent.Y, FMath::Abs(Point.Z - Center.Z) - HalfExtent.Z);
        const FIntVector Outside = Delta.ComponentMax(FIntVector::ZeroValue);
        return IntegerSqrt(LengthSquared(Outside)) + FMath::Min(Delta.GetMax(), 0);
    }

    uint32 HashSample(uint32 Hash, int16 Sample)
    {
        return (Hash ^ static_cast<uint16>(Sample)) * 16777619u;
    }
}

FIntVector VoxelPlayground::QuantizeNormal(const FVector& Normal)
{
    return VoxelSurfaceKernel::QuantizeNormal(Normal);
}

namespace
{
    int32 BurialDistance(FIntVector Local, int32 Layer)
    {
        // A single 40 cm course. Adjacent courses overlap and do not replace dug-out earlier courses.
        const int32 Radius = IntegerSqrt(Local.X * Local.X + Local.Y * Local.Y);
        const int32 Top = Layer * 40;
        const int32 Bottom = (Layer - 1) * 40 - 16;
        const int32 Outer = FMath::Max3(Radius - 106, Local.Z - Top, Bottom - Local.Z);
        // An open-bottom cavity preserves the original floor, and 30 cm of roof remains at layer 3.
        return FMath::Max(Outer, FMath::Min(55 - Radius, 90 - Local.Z));
    }
}

FBox VoxelPlayground::PartyBrushBounds(const FVoxelEdit& Edit)
{
    const FVector Center(Edit.Center);
    if (Edit.BrushId == ShallowCut) return FBox::BuildAABB(Center, FVector(Edit.PunchRadius()+28.));
    if (Edit.BrushId == MudPad)
    {
        const double Width = 110. * Edit.FootprintScale();
        return FBox(Center - FVector(Width,Width,65.), Center + FVector(Width,Width,45.));
    }
    const int32 Top = 40 * (Edit.BrushId - FirstBurialLayer + 1);
    return FBox(Center + FVector(-108.,-108.,Top - 56.), Center + FVector(108.,108.,Top));
}

int32 VoxelPlayground::PartyBrushDistance(const FVoxelEdit& Edit, FIntVector World)
{
    const FIntVector Local = World - Edit.Center;
    const FIntVector N = Edit.SurfaceNormal;
    if (Edit.BrushId == ShallowCut)
        return VoxelSurfaceKernel::ToolDistanceWithRadius(Local, N, Edit.PunchRadius(), PartyPunch::DepthCm(Edit.PunchPower));
    int32 Distance = IsBurial(Edit.BrushId)
        ? BurialDistance(Local, Edit.BrushId - FirstBurialLayer + 1)
        : VoxelSurfaceKernel::ToolDistance(Local,N,Edit.BrushId == MudPad,Edit.FootprintScale());
    for (const auto& Box : Edit.ProtectedBoxes)
    {
        const int32 ToBox = FMath::Max((Box.Low - World).GetMax(), (World - Box.High).GetMax());
        Distance = FMath::Max(Distance, -ToBox);
    }
    for (const FVoxelMudClearance& Void : Edit.MudClearances)
    {
        FIntVector Delta = World - Void.Center;
        Delta.Z = FMath::Max(0, FMath::Abs(Delta.Z) - Void.AxisHalfLength);
        Distance = FMath::Max(Distance, Void.Radius - IntegerSqrt(LengthSquared(Delta)));
    }
    return Distance;
}

uint8 VoxelPlayground::BrushFor(EVoxelAction Action)
{
    return Action == EVoxelAction::Fill ? 1 : 0;
}

int32 VoxelPlayground::RadiusFor(uint8 BrushId)
{
    if (BrushId == ShallowCut) return 62;
    if (BrushId == MudPad) return 110;
    if (IsBurial(BrushId)) return 165;
    return BrushId == 1 ? 75 : 125;
}

FIntVector VoxelPlayground::QuantizeCenter(const FVector& Position)
{
    return VoxelSurfaceKernel::QuantizeCenter(Position);
}

int32 VoxelPlayground::ShoreDistance(int32 X, int32 Y)
{
    const int32 AX = FMath::Abs(X), AY = FMath::Abs(Y);
    return FMath::Max(FMath::Max(AX, AY) - 2300, (AX + AY - 3250) * 7 / 10);
}

int32 VoxelPlayground::IslandSurfaceHeight(int32 X, int32 Y)
{
    const int32 Plateau = 460 + FMath::Max(0, 85 - (FMath::Abs(X) + FMath::Abs(Y)) / 12);
    return FMath::Clamp(-ShoreDistance(X, Y) * 11 / 20 - 25, -25, Plateau);
}

int32 VoxelPlayground::ShoreExtent(EVoxelLayout Layout)
{
    return Layout == EVoxelLayout::Expedition ? 8000 : 2300;
}

int32 VoxelPlayground::ShoreDistance(EVoxelLayout Layout, int32 X, int32 Y)
{
    if (Layout != EVoxelLayout::Expedition) return ShoreDistance(X, Y);
    const int32 AX = FMath::Abs(X), AY = FMath::Abs(Y);
    return FMath::Max(FMath::Max(AX, AY) - 8000, (AX + AY - 11300) * 7 / 10);
}

TArray<FVector2D> VoxelPlayground::CoastOutline(EVoxelLayout Layout)
{
    const double R = ShoreExtent(Layout), K = Layout == EVoxelLayout::Expedition ? 3300.0 : 950.0;
    return {{R,K},{K,R},{-K,R},{-R,K},{-R,-K},{-K,-R},{K,-R},{R,-K}};
}

int32 VoxelPlayground::BaseHeight(EVoxelLayout Layout, int32 X, int32 Y)
{
    if (Layout != EVoxelLayout::Expedition) return IslandSurfaceHeight(X, Y);
    const int32 AX = FMath::Abs(X), AY = FMath::Abs(Y);
    const int32 Side = FMath::Max(AX, AY);
    // Low radial approaches split around the four excavation shelves. A raised central crown
    // makes the final dig deep enough and keeps the rock silhouettes visible above the coast.
    const int32 ValleyDistance = FMath::Min3(AX, AY, FMath::Abs(AX - AY) * 7 / 10);
    const int32 Shelf = 370 + FMath::Min(460, ValleyDistance / 3)
        + FMath::Clamp((2300 - Side) / 4, 0, 340);
    const int32 Shore = ShoreDistance(Layout, X, Y);
    // The contour trail stays below the rough shelves and intersects all eight incoming valleys.
    const int32 TrailDepth = FMath::Clamp(165 - FMath::Abs(-Shore - 2050) / 3, 0, 165);
    return FMath::Clamp(-Shore / 4 - 12, -25, Shelf) - TrailDepth;
}

FVoxelGrid FVoxelGrid::ForLayout(EVoxelLayout Layout)
{
    if (Layout == EVoxelLayout::Expedition) return {{-8400, -8400, -800}, {672, 672, 80}};
    return Layout == EVoxelLayout::Island ? FVoxelGrid{{-2400, -2400, -800}, {192, 192, 64}}
        : FVoxelGrid{{0, -600, -400}, {64, 48, 32}};
}

int32 FVoxelGrid::NumChunks() const
{
    const FIntVector Count = Chunks();
    return Count.X * Count.Y * Count.Z;
}

FIntVector FVoxelGrid::ChunkCoordinates(int32 Index) const
{
    const FIntVector Count = Chunks();
    return FIntVector(Index % Count.X, (Index / Count.X) % Count.Y, Index / (Count.X * Count.Y));
}

bool FVoxelGrid::Contains(const FIntVector& Point) const
{
    return Point.GetMin() >= 0 && Point.X <= Cells.X && Point.Y <= Cells.Y && Point.Z <= Cells.Z;
}

int32 FVoxelGrid::SampleIndex(const FIntVector& Point) const
{
    return Point.X + (Cells.X + 1) * (Point.Y + (Cells.Y + 1) * Point.Z);
}

FIntVector FVoxelGrid::SampleCoordinates(int32 Index) const
{
    const int32 Row = Index / (Cells.X + 1);
    return {Index % (Cells.X + 1), Row % (Cells.Y + 1), Row / (Cells.Y + 1)};
}

bool FVoxelGrid::IsValidEdit(const FVoxelEdit& Edit) const
{
    using namespace VoxelPlayground;
    const bool bPartyBrush = Edit.BrushId >= ShallowCut && Edit.BrushId <= LastBurialLayer;
    const bool bCorrectBrush = bPartyBrush
        ? Edit.Action == (Edit.BrushId == ShallowCut ? EVoxelAction::Punch : EVoxelAction::Fill)
        : Edit.BrushId == BrushFor(Edit.Action);
    if (Edit.Action > EVoxelAction::Fill || !bCorrectBrush || (Edit.PunchPower && Edit.BrushId != ShallowCut)
        || (Edit.bWideFootprint && Edit.BrushId != ShallowCut && Edit.BrushId != MudPad)
        || (bPartyBrush && !Edit.ExtraFillCenters.IsEmpty())
        || ((!bPartyBrush || Edit.Action != EVoxelAction::Fill) && !Edit.ProtectedBoxes.IsEmpty())
        || (Edit.BrushId != MudPad && !Edit.MudClearances.IsEmpty()) || Edit.MudClearances.Num() > 32
        || Edit.ProtectedBoxes.Num() > 32
        || Edit.ExtraFillCenters.Num() >= FVoxelEdit::MaxFillCenters
        || (Edit.Action != EVoxelAction::Fill && !Edit.ExtraFillCenters.IsEmpty()))
    {
        return false;
    }
    if (bPartyBrush)
    {
        if (Edit.SurfaceNormal.GetMax() > 1000 || Edit.SurfaceNormal.GetMin() < -1000) return false;
        const int32 NormalSquared = LengthSquared(Edit.SurfaceNormal);
        if (NormalSquared < 990000 || NormalSquared > 1010000) return false;
    }
    for (const FVoxelMudClearance& Void : Edit.MudClearances)
        if (Void.Radius < 1 || Void.Radius > 256 || Void.AxisHalfLength < 0 || Void.AxisHalfLength > 256
            || FVector::DistSquared(FVector(Void.Center), FVector(Edit.Center)) > FMath::Square(1024.)) return false;
    const int32 Radius = Edit.BrushId==ShallowCut ? Edit.PunchRadius() : RadiusFor(Edit.BrushId) * Edit.FootprintScale();
    const FIntVector Max = Origin + Cells * Spacing;
    const auto InBounds = [&](const FIntVector& P)
    {
        return P.X >= Origin.X - Radius && P.X <= Max.X + Radius
            && P.Y >= Origin.Y - Radius && P.Y <= Max.Y + Radius
            && P.Z >= Origin.Z - Radius && P.Z <= Max.Z + Radius;
    };
    if (!InBounds(Edit.Center)) return false;
    for (const auto& P : Edit.ExtraFillCenters) if (!InBounds(P)) return false;
    return true;
}

FVoxelDensity::FVoxelDensity(EVoxelLayout InLayout) : Grid(FVoxelGrid::ForLayout(InLayout)), Layout(InLayout)
{
    using namespace VoxelPlayground;
    const FIntVector Cells = Grid.Cells;
    Foundation.Reserve((Cells.X + 1) * (Cells.Y + 1));
    if (Layout == EVoxelLayout::Expedition)
    {
        BaseColumns.Reserve((Cells.X + 1) * (Cells.Y + 1));
        uint32 Shape = 2166136261u;
        for (int32 Y = 0; Y <= Cells.Y; ++Y)
        {
            for (int32 X = 0; X <= Cells.X; ++X)
            {
                const int32 PX = Grid.Origin.X + X * Spacing, PY = Grid.Origin.Y + Y * Spacing;
                const FIntPoint Column(ShoreDistance(PX, PY), BaseHeight(Layout, PX, PY));
                BaseColumns.Add(Column);
                Foundation.Add(Column.X < 0);
                Shape = HashSample(HashSample(Shape, Column.X), Column.Y);
            }
        }
        SparseChecksum = Shape;
        return;
    }
    Samples.SetNumUninitialized((Cells.X + 1) * (Cells.Y + 1) * (Cells.Z + 1));
    for (int32 Y = 0; Y <= Cells.Y; ++Y)
    {
        for (int32 X = 0; X <= Cells.X; ++X)
        {
            const FIntVector Ground = Grid.Origin + FIntVector(X, Y, 0) * Spacing;
            const int32 Shore = ShoreDistance(Ground.X, Ground.Y);
            const int32 Top = IslandSurfaceHeight(Ground.X, Ground.Y);
            for (int32 Z = 0; Z <= Cells.Z; ++Z)
            {
                const FIntVector GridPoint(X, Y, Z);
                const FIntVector Point = Ground + FIntVector(0, 0, Z * Spacing);
                int32 Density;
                if (Layout == EVoxelLayout::Island)
                {
                    Density = FMath::Max3(Shore, Point.Z - Top, -775 - Point.Z);
                }
                else
                {
                    // The original playground fixtures retain their exact sample field.
                    Density = BoxDistance(Point, {790, 0, -195}, {790, 575, 195});
                    Density = FMath::Min(Density, BoxDistance(Point, {400, -215, 130}, {50, 310, 150}));
                    Density = FMath::Min(Density, BoxDistance(Point, {820, -300, 12}, {150, 155, 28}));
                    Density = FMath::Min(Density, BoxDistance(Point, {1030, 170, 140}, {100, 105, 160}));
                    Density = FMath::Min(Density, BoxDistance(Point, {1030, 495, 140}, {100, 75, 160}));
                }
                Samples[Grid.SampleIndex(GridPoint)] = FMath::Clamp(Density, -VoxelSurfaceKernel::Band, VoxelSurfaceKernel::Band);
            }
            Foundation.Add(Samples[Grid.SampleIndex({X,Y,FoundationLayer(Layout)})] < 0);
        }
    }
}

int16 FVoxelDensity::BaseSample(const FIntVector& Point) const
{
    const FIntPoint& Column = BaseColumns[Point.X + Point.Y * (Grid.Cells.X + 1)];
    const int32 Z = Grid.Origin.Z + Point.Z * Spacing;
    return FMath::Clamp(FMath::Max3(Column.X, Z - Column.Y, -775 - Z), -VoxelSurfaceKernel::Band, VoxelSurfaceKernel::Band);
}

namespace
{
    constexpr int32 BlockSide = VoxelPlayground::CellsPerChunk;
    int32 BlockOffset(const FIntVector& Point)
    {
        return Point.X % BlockSide + BlockSide * (Point.Y % BlockSide + BlockSide * (Point.Z % BlockSide));
    }
}

int32 FVoxelDensity::BlockKey(const FIntVector& Point) const
{
    const FIntVector Block = Point / BlockSide;
    const FIntVector Width = Grid.Chunks() + FIntVector(1);
    return Block.X + Width.X * (Block.Y + Width.Y * Block.Z);
}

TArray<int16>& FVoxelDensity::GetOrCreateBlock(const FIntVector& Point)
{
    TArray<int16>& Block = EditedBlocks.FindOrAdd(BlockKey(Point));
    if (Block.IsEmpty())
    {
        const FIntVector Start = Point / BlockSide * BlockSide;
        Block.Init(VoxelSurfaceKernel::Band, BlockSide * BlockSide * BlockSide);
        for (int32 Z = 0; Z < BlockSide && Start.Z + Z <= Grid.Cells.Z; ++Z)
            for (int32 Y = 0; Y < BlockSide && Start.Y + Y <= Grid.Cells.Y; ++Y)
                for (int32 X = 0; X < BlockSide && Start.X + X <= Grid.Cells.X; ++X)
                    Block[X + BlockSide * (Y + BlockSide * Z)] = BaseSample(Start + FIntVector(X, Y, Z));
    }
    return Block;
}

void FVoxelDensity::CopySampleRow(FIntVector Start, int32 Count, int16* Destination) const
{
    const int32 MinX = FMath::Clamp(-Start.X, 0, Count);
    const int32 MaxX = FMath::Clamp(Grid.Cells.X - Start.X + 1, 0, Count);
    if (MinX == MaxX || Start.Y < 0 || Start.Z < 0 || Start.Y > Grid.Cells.Y || Start.Z > Grid.Cells.Z)
    {
        for (int32 I = 0; I < Count; ++I) Destination[I] = VoxelSurfaceKernel::Band;
        return;
    }
    for (int32 I = 0; I < MinX; ++I) Destination[I] = VoxelSurfaceKernel::Band;
    for (int32 I = MaxX; I < Count; ++I) Destination[I] = VoxelSurfaceKernel::Band;
    Start.X += MinX;
    if (Layout != EVoxelLayout::Expedition)
    {
        FMemory::Memcpy(Destination + MinX, Samples.GetData() + Grid.SampleIndex(Start), (MaxX - MinX) * sizeof(int16));
        return;
    }
    for (int32 I = MinX; I < MaxX;)
    {
        const int32 Span = FMath::Min(BlockSide - Start.X % BlockSide, MaxX - I);
        if (const TArray<int16>* Block = EditedBlocks.Find(BlockKey(Start)))
        {
            FMemory::Memcpy(Destination + I, Block->GetData() + BlockOffset(Start), Span * sizeof(int16));
        }
        else
        {
            for (int32 X = 0; X < Span; ++X) Destination[I + X] = BaseSample(Start + FIntVector(X, 0, 0));
        }
        I += Span;
        Start.X += Span;
    }
}

int16 FVoxelDensity::Sample(const FIntVector& GridPoint, const FVoxelDensityPatch* Patch) const
{
    if (!Grid.Contains(GridPoint))
    {
        return VoxelSurfaceKernel::Band;
    }
    const int32 Index = Grid.SampleIndex(GridPoint);
    if (const int16* Patched = Patch ? Patch->Samples.Find(Index) : nullptr)
    {
        return *Patched;
    }
    if (Layout == EVoxelLayout::Expedition)
    {
        if (const auto* Block = EditedBlocks.Find(BlockKey(GridPoint))) return (*Block)[BlockOffset(GridPoint)];
        return BaseSample(GridPoint);
    }
    return Samples[Index];
}

FVoxelDensityPatch FVoxelDensity::PrepareEdit(const FVoxelEdit& Edit) const
{
    const int32 Radius = VoxelPlayground::RadiusFor(Edit.BrushId);
    const bool bAdd = Edit.Action == EVoxelAction::Fill;
    bool bTouchesSolid = false;
    const FVoxelEdit* PartyBrush = Edit.BrushId >= VoxelPlayground::ShallowCut ? &Edit : nullptr;
    auto Result = PrepareStamp({Edit.Center, Radius, bAdd}, PartyBrush, bTouchesSolid);
    for (const FIntVector& Center : Edit.ExtraFillCenters)
    {
        auto Lobe = PrepareStamp({Center, Radius, true}, nullptr, bTouchesSolid);
        for (const auto& Entry : Lobe.Samples)
        {
            if (int16* Previous = Result.Samples.Find(Entry.Key))
                *Previous = FMath::Min(*Previous, Entry.Value);
            else
                Result.Samples.Add(Entry.Key, Entry.Value);
        }
    }
    // A whole spill is one edit. A lobe may be supported through another lobe.
    ResolveSupport(Result, bAdd, bTouchesSolid);
    return Result;
}

FVoxelDensityPatch FVoxelDensity::PrepareBrush(const FVoxelBrushStamp& Stamp) const
{
    bool bTouchesSolid = false;
    auto Patch = PrepareStamp(Stamp, nullptr, bTouchesSolid);
    ResolveSupport(Patch, Stamp.bAdd, bTouchesSolid);
    return Patch;
}

FVoxelDensityPatch FVoxelDensity::PrepareStamp(const FVoxelBrushStamp& Stamp, const FVoxelEdit* PartyEdit, bool& bTouchesSolid) const
{
    using namespace VoxelPlayground;
    const int32 Radius = Stamp.Radius;
    const int32 Reach = Radius + VoxelSurfaceKernel::Band;
    const FIntVector Relative = Stamp.Center - Grid.Origin;
    const FIntVector Extent(Reach);
    FIntVector Low = ((Relative - Extent) / Spacing).ComponentMax(FIntVector(1));
    FIntVector High = ((Relative + Extent) / Spacing).ComponentMin(Grid.Cells - FIntVector(1));
    if (PartyEdit)
    {
        const FBox Bounds = PartyBrushBounds(*PartyEdit).ExpandBy(VoxelSurfaceKernel::Band + Spacing);
        Low = (FIntVector(Bounds.Min - FVector(Grid.Origin)) / Spacing).ComponentMax(FIntVector(1));
        High = (FIntVector(Bounds.Max - FVector(Grid.Origin)) / Spacing).ComponentMin(Grid.Cells - FIntVector(1));
    }
    FVoxelDensityPatch Patch;
    for (int32 Z = Low.Z; Z <= High.Z; ++Z)
    {
        for (int32 Y = Low.Y; Y <= High.Y; ++Y)
        {
            for (int32 X = Low.X; X <= High.X; ++X)
            {
                const FIntVector Point(X, Y, Z);
                const FIntVector World = Grid.Origin + Point * Spacing;
                if (Layout != EVoxelLayout::Playground && Stamp.bAdd && ShoreDistance(World.X, World.Y) >= 0)
                {
                    continue;
                }
                const int32 Shape = PartyEdit ? PartyBrushDistance(*PartyEdit, World)
                    : IntegerSqrt(LengthSquared(Point * Spacing - Relative)) - Radius;
                const int32 Index = Grid.SampleIndex(Point);
                const int16 Value = Sample(Point);
                if (Stamp.bAdd && Shape < 0 && Value < 0) bTouchesSolid = true;
                const int16 Updated = VoxelSurfaceKernel::Blend(Value, Shape, Stamp.bAdd);
                if (Updated != Value)
                {
                    Patch.Samples.Add(Index, Updated);
                }
            }
        }
    }
    return Patch;
}

void FVoxelDensity::ResolveSupport(FVoxelDensityPatch& Patch, bool bAdd, bool bTouchesSolid) const
{
    using namespace VoxelSurfaceKernel;
    if (Patch.Samples.IsEmpty()) return;
    TArray<FIntVector> Seeds;
    for (const auto& Change : Patch.Samples)
    {
        const FIntVector Point = Grid.SampleCoordinates(Change.Key);
        const bool bWasSolid = Sample(Point) < 0;
        if (bWasSolid == (Change.Value < 0)) continue;
        if (bAdd) Seeds.Add(Point);
        else for (FIntVector Offset : FaceNeighbors) Seeds.Add(Point + Offset);
    }

    const auto IsSolid = [&](FIntVector Point) { return Sample(Point, &Patch) < 0; };
    const int32 FloorZ = FoundationLayer(Layout);
    const auto IsRoot = [&](FIntVector Point)
    {
        // Adding material cannot sever any existing support path.
        return (bAdd && Sample(Point) < 0)
            || (Point.Z == FloorZ && Foundation[Point.X + (Grid.Cells.X + 1) * Point.Y]);
    };
    int32 Removed = 0;
    FindUnsupported(Seeds, IsSolid, IsRoot, [&](FIntVector Point)
    {
        ++Removed;
        const int32 Index = Grid.SampleIndex(Point);
        if (bAdd) Patch.Samples.Remove(Index);
        else Patch.Samples.Add(Index, VoxelSurfaceKernel::Band);
    });
    if (bAdd && !bTouchesSolid && Removed == Seeds.Num())
    {
        // A brush in the air also changes positive distance samples around its shape.
        // None of those samples or a sequence number should survive a rejected fill.
        Patch.Samples.Reset();
        return;
    }
    if (bAdd && Removed > 0)
    {
        // A multi-lobe or clipped brush can contain both attached and detached material.
        // Keep distance samples close enough to affect a retained surface or its normals.
        for (auto It = Patch.Samples.CreateIterator(); It; ++It)
        {
            if (It.Value() < 0) continue;
            const FIntVector Point = Grid.SampleCoordinates(It.Key());
            bool bNearSolid = false;
            for (int32 Z = -2; Z <= 2 && !bNearSolid; ++Z)
                for (int32 Y = -2; Y <= 2 && !bNearSolid; ++Y)
                    for (int32 X = -2; X <= 2 && !bNearSolid; ++X)
                        bNearSolid = IsSolid(Point + FIntVector(X, Y, Z));
            if (!bNearSolid) It.RemoveCurrent();
        }
    }

    // Collapses may reach chunks well beyond the original brush. Update each affected
    // mesh section, including neighboring sections whose vertex normals read this sample.
    const FIntVector Chunks = Grid.Chunks();
    TSet<int32> Dirty;
    for (const auto& Change : Patch.Samples)
    {
        const FIntVector Point = Grid.SampleCoordinates(Change.Key);
        const FIntVector First = ((Point - FIntVector(2)) / VoxelPlayground::CellsPerChunk).ComponentMax(FIntVector::ZeroValue);
        const FIntVector Last = ((Point + FIntVector(1)) / VoxelPlayground::CellsPerChunk).ComponentMin(Chunks - FIntVector(1));
        for (int32 Z = First.Z; Z <= Last.Z; ++Z)
            for (int32 Y = First.Y; Y <= Last.Y; ++Y)
                for (int32 X = First.X; X <= Last.X; ++X)
                    Dirty.Add(X + Y * Chunks.X + Z * Chunks.X * Chunks.Y);
    }
    Patch.Chunks = Dirty.Array();
    Patch.Chunks.Sort();
}

void FVoxelDensity::Apply(const FVoxelDensityPatch& Patch)
{
    if (Layout == EVoxelLayout::Expedition)
    {
        const int32 Columns = Grid.Chunks().X * Grid.Chunks().Y;
        for (int32 Index : Patch.Chunks) PromotedColumns.Add(Index % Columns);
        for (const TPair<int32, int16>& Change : Patch.Samples)
        {
            const int32 Key = Change.Key;
            const FIntVector Point = Grid.SampleCoordinates(Key);
            const int16 Base = BaseSample(Point);
            const auto EntryHash = [Key](int16 Value){ return HashCombineFast(GetTypeHash(Key), GetTypeHash(Value)); };
            TArray<int16>& Block = GetOrCreateBlock(Point);
            int16& Old = Block[BlockOffset(Point)];
            if (Old != Base) SparseChecksum ^= EntryHash(Old);
            Old = Change.Value;
            if (Old != Base) SparseChecksum ^= EntryHash(Old);
        }
        return;
    }
    for (const TPair<int32, int16>& Change : Patch.Samples)
    {
        Samples[Change.Key] = Change.Value;
    }
}

bool FVoxelDensity::IsVolumeClear(const FBox& Box) const
{
    return VolumeMatches(Box, false);
}

bool FVoxelDensity::IsVolumeSolid(const FBox& Box) const
{
    return VolumeMatches(Box, true);
}

bool FVoxelDensity::VolumeMatches(const FBox& Box, bool bSolid) const
{
    using namespace VoxelPlayground;
    const FVector Min = (Box.Min - FVector(Grid.Origin)) / Spacing;
    const FVector Max = (Box.Max - FVector(Grid.Origin)) / Spacing;
    for (int32 Z = FMath::FloorToInt(Min.Z); Z <= FMath::CeilToInt(Max.Z); ++Z)
    {
        for (int32 Y = FMath::FloorToInt(Min.Y); Y <= FMath::CeilToInt(Max.Y); ++Y)
        {
            for (int32 X = FMath::FloorToInt(Min.X); X <= FMath::CeilToInt(Max.X); ++X)
            {
                if ((Sample({X, Y, Z}) < 0) != bSolid)
                {
                    return false;
                }
            }
        }
    }
    return true;
}

double FVoxelDensity::SignedDistanceAt(FVector World) const
{
    const FVector P = (World - FVector(Grid.Origin)) / VoxelPlayground::Spacing;
    const FIntVector Low(FMath::FloorToInt(P.X), FMath::FloorToInt(P.Y), FMath::FloorToInt(P.Z));
    if (!Grid.Contains(Low)) return VoxelSurfaceKernel::Band;
    const FVector T = P - FVector(Low);
    auto Plane = [&](int32 Z)
    {
        const double A = FMath::Lerp<double>(Sample({Low.X, Low.Y, Z}), Sample({Low.X+1, Low.Y, Z}), T.X);
        const double B = FMath::Lerp<double>(Sample({Low.X, Low.Y+1, Z}), Sample({Low.X+1, Low.Y+1, Z}), T.X);
        return FMath::Lerp(A, B, T.Y);
    };
    return FMath::Lerp(Plane(Low.Z), Plane(Low.Z+1), T.Z);
}

TOptional<double> FVoxelDensity::SurfaceAt(FVector2D Position) const
{
    using namespace VoxelPlayground;
    const FVector2D Local = (Position - FVector2D(Grid.Origin.X, Grid.Origin.Y)) / Spacing;
    const int32 X = FMath::FloorToInt(Local.X);
    const int32 Y = FMath::FloorToInt(Local.Y);
    if (X < 0 || X >= Grid.Cells.X || Y < 0 || Y >= Grid.Cells.Y)
    {
        return {};
    }

    const auto Column = [&](int32 Z)
    {
        const double A = FMath::Lerp<double>(Sample({X, Y, Z}), Sample({X + 1, Y, Z}), Local.X - X);
        const double B = FMath::Lerp<double>(Sample({X, Y + 1, Z}), Sample({X + 1, Y + 1, Z}), Local.X - X);
        return FMath::Lerp(A, B, Local.Y - Y);
    };
    int32 Top = Grid.Cells.Z;
    if (Layout == EVoxelLayout::Expedition)
    {
        // A footprint outside edited tiles still has the original height field.
        // Its highest corner bounds the first possible crossing exactly. This skips
        // empty sky while sampling thousands of candidate routes at round start.
        const auto Chunks = Grid.Chunks();
        const int32 Tile = (X / CellsPerChunk) + (Y / CellsPerChunk) * Chunks.X;
        if (!PromotedColumns.Contains(Tile))
        {
            const int32 Row = Grid.Cells.X + 1, Index = X + Y * Row;
            const auto& A = BaseColumns[Index];
            const auto& B = BaseColumns[Index+1];
            const auto& C = BaseColumns[Index+Row];
            const auto& D = BaseColumns[Index+Row+1];
            if (FMath::Min(FMath::Min(A.X,B.X), FMath::Min(C.X,D.X)) >= 0) return {};
            const int32 Height = FMath::Max(FMath::Max(A.Y,B.Y), FMath::Max(C.Y,D.Y));
            Top = FMath::Clamp(FMath::DivideAndRoundUp(Height - Grid.Origin.Z, Spacing), 0, Top);
        }
    }
    double Above = Column(Top);
    for (int32 Z = Top - 1; Z >= 0; --Z)
    {
        const double Below = Column(Z);
        if (Below < 0.0 && Above >= 0.0)
        {
            return Grid.Origin.Z + (Z - Below / (Above - Below)) * Spacing;
        }
        Above = Below;
    }
    return {};
}

uint32 FVoxelDensity::Checksum() const
{
    if (Layout == EVoxelLayout::Expedition) return SparseChecksum;
    uint32 Hash = 2166136261u;
    for (int16 Value : Samples)
    {
        Hash = HashSample(Hash, Value);
    }
    return Hash;
}

FString FVoxelDensity::ChunkChecksums() const
{
    FString Result;
    for (int32 Index = 0; Index < Grid.NumChunks(); ++Index)
    {
        if (Layout == EVoxelLayout::Expedition && !PromotedColumns.Contains(Index % (Grid.Chunks().X * Grid.Chunks().Y))) continue;
        const FIntVector First = Grid.ChunkCoordinates(Index) * CellsPerChunk;
        uint32 Hash = 2166136261u;
        for (int32 Z = 0; Z <= CellsPerChunk; ++Z)
        {
            for (int32 Y = 0; Y <= CellsPerChunk; ++Y)
            {
                for (int32 X = 0; X <= CellsPerChunk; ++X)
                {
                    Hash = HashSample(Hash, Sample(First + FIntVector(X, Y, Z)));
                }
            }
        }
        Result += FString::Printf(TEXT("%s%08x"), Result.IsEmpty() ? TEXT("") : TEXT(","), Hash);
    }
    return Result;
}
