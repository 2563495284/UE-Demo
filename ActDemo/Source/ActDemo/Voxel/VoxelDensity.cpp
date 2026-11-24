#include "Voxel/VoxelDensity.h"

namespace
{
    using namespace VoxelPlayground;
    constexpr int32 Band = 2 * Spacing;
    // Evaluate sphere distance with integer arithmetic on every peer.
    int32 IntegerSqrt(int32 Value)
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

uint8 VoxelPlayground::BrushFor(EVoxelAction Action)
{
    return Action == EVoxelAction::Fill ? 1 : 0;
}

int32 VoxelPlayground::RadiusFor(uint8 BrushId)
{
    return BrushId == 1 ? 75 : 125;
}

FIntVector VoxelPlayground::QuantizeCenter(const FVector& Position)
{
    constexpr int32 CenterStep = 5;
    return FIntVector(FMath::RoundToInt(Position.X / CenterStep), FMath::RoundToInt(Position.Y / CenterStep),
        FMath::RoundToInt(Position.Z / CenterStep)) * CenterStep;
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

bool FVoxelGrid::IsValidEdit(const FVoxelEdit& Edit) const
{
    using namespace VoxelPlayground;
    if (Edit.Action > EVoxelAction::Fill || Edit.BrushId != BrushFor(Edit.Action))
    {
        return false;
    }
    const int32 Radius = RadiusFor(Edit.BrushId);
    const FIntVector Max = Origin + Cells * Spacing;
    return Edit.Center.X >= Origin.X - Radius && Edit.Center.X <= Max.X + Radius
        && Edit.Center.Y >= Origin.Y - Radius && Edit.Center.Y <= Max.Y + Radius
        && Edit.Center.Z >= Origin.Z - Radius && Edit.Center.Z <= Max.Z + Radius;
}

FVoxelDensity::FVoxelDensity(EVoxelLayout InLayout) : Grid(FVoxelGrid::ForLayout(InLayout)), Layout(InLayout)
{
    using namespace VoxelPlayground;
    const FIntVector Cells = Grid.Cells;
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
                Samples[Grid.SampleIndex(GridPoint)] = FMath::Clamp(Density, -Band, Band);
            }
        }
    }
}

int16 FVoxelDensity::BaseSample(const FIntVector& Point) const
{
    const FIntPoint& Column = BaseColumns[Point.X + Point.Y * (Grid.Cells.X + 1)];
    const int32 Z = Grid.Origin.Z + Point.Z * Spacing;
    return FMath::Clamp(FMath::Max3(Column.X, Z - Column.Y, -775 - Z), -Band, Band);
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
        Block.Init(Band, BlockSide * BlockSide * BlockSide);
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
        for (int32 I = 0; I < Count; ++I) Destination[I] = Band;
        return;
    }
    for (int32 I = 0; I < MinX; ++I) Destination[I] = Band;
    for (int32 I = MaxX; I < Count; ++I) Destination[I] = Band;
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
        return Band;
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
    return PrepareBrush({Edit.Center, VoxelPlayground::RadiusFor(Edit.BrushId), Edit.Action == EVoxelAction::Fill});
}

FVoxelDensityPatch FVoxelDensity::PrepareBrush(const FVoxelBrushStamp& Stamp) const
{
    using namespace VoxelPlayground;
    const int32 Radius = Stamp.Radius;
    const int32 Reach = Radius + Band;
    const FIntVector Relative = Stamp.Center - Grid.Origin;
    const FIntVector Extent(Reach);
    const FIntVector Low = ((Relative - Extent) / Spacing).ComponentMax(FIntVector(1));
    const FIntVector High = ((Relative + Extent) / Spacing).ComponentMin(Grid.Cells - FIntVector(1));
    FIntVector MinChanged = Grid.Cells;
    FIntVector MaxChanged = FIntVector(-1);
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
                const int32 Distance = FMath::Clamp(IntegerSqrt(LengthSquared(Point * Spacing - Relative)) - Radius, -Band, Band);
                const int32 Index = Grid.SampleIndex(Point);
                const int16 Value = Sample(Point);
                const int16 Updated = Stamp.bAdd
                    ? FMath::Min<int16>(Value, Distance) : FMath::Max<int16>(Value, -Distance);
                if (Updated != Value)
                {
                    Patch.Samples.Add(Index, Updated);
                    MinChanged = MinChanged.ComponentMin(Point);
                    MaxChanged = MaxChanged.ComponentMax(Point);
                }
            }
        }
    }
    if (Patch.Samples.IsEmpty())
    {
        return Patch;
    }

    // A surface reads its corners and the neighbouring samples used for normals.
    const FIntVector Chunks = Grid.Chunks();
    const FIntVector First = ((MinChanged - FIntVector(2)) / CellsPerChunk).ComponentMax(FIntVector::ZeroValue);
    const FIntVector Last = ((MaxChanged + FIntVector(1)) / CellsPerChunk).ComponentMin(Chunks - FIntVector(1));
    for (int32 Z = First.Z; Z <= Last.Z; ++Z)
    {
        for (int32 Y = First.Y; Y <= Last.Y; ++Y)
        {
            for (int32 X = First.X; X <= Last.X; ++X)
            {
                Patch.Chunks.Add(X + Y * Chunks.X + Z * Chunks.X * Chunks.Y);
            }
        }
    }
    return Patch;
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
            const int32 Row = Key / (Grid.Cells.X + 1);
            const FIntVector Point(Key % (Grid.Cells.X + 1), Row % (Grid.Cells.Y + 1), Row / (Grid.Cells.Y + 1));
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
    double Above = Column(Grid.Cells.Z);
    for (int32 Z = Grid.Cells.Z - 1; Z >= 0; --Z)
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
