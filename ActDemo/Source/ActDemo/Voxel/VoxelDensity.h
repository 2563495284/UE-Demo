#pragma once

#include "CoreMinimal.h"
#include "PartyPunch.h"
#include "VoxelDensity.generated.h"

UENUM()
enum class EVoxelAction : uint8
{
    Punch,
    DigDown,
    Fill
};

UENUM()
enum class EVoxelLayout : uint8
{
    Playground,
    Island,
    Expedition
};

USTRUCT()
struct FVoxelProtectedBox
{
    GENERATED_BODY()
    UPROPERTY() FIntVector Low = FIntVector::ZeroValue;
    UPROPERTY() FIntVector High = FIntVector::ZeroValue;
};

USTRUCT()
struct FVoxelEdit
{
    GENERATED_BODY()

    UPROPERTY() uint32 Sequence = 0;
    UPROPERTY() EVoxelAction Action = EVoxelAction::Punch;
    UPROPERTY() FIntVector Center = FIntVector::ZeroValue;
    UPROPERTY() uint8 BrushId = 0;
    // The server records the footprint with the edit, so replay does not need the current crew buff.
    UPROPERTY() bool bWideFootprint = false;
    int32 FootprintScale() const { return bWideFootprint ? 2 : 1; }
    UPROPERTY() uint8 PunchPower = 0;
    int32 PunchRadius() const { return FMath::RoundToInt(62.f*PartyPunch::RadiusScale(PunchPower,bWideFootprint)); }
    // Surface direction as fixed-point thousandths; all peers evaluate the same cut.
    UPROPERTY() FIntVector SurfaceNormal = FIntVector(0, 0, 1000);
    UPROPERTY() TArray<FVoxelProtectedBox> ProtectedBoxes;
    // All lobes of an environmental sand spill share one sequence and collision commit.
    UPROPERTY() TArray<FIntVector> ExtraFillCenters;
    static constexpr int32 MaxFillCenters = 128;
};

USTRUCT()
struct FVoxelBrushStamp
{
    GENERATED_BODY()

    UPROPERTY() FIntVector Center = FIntVector::ZeroValue;
    UPROPERTY() int32 Radius = 125;
    UPROPERTY() bool bAdd = false;
};

namespace VoxelPlayground
{
    inline constexpr int32 Spacing = 25;
    inline constexpr int32 CellsPerChunk = 16;
    inline constexpr int32 ChunkCount = 24;

    constexpr uint8 ShallowCut = 2;
    constexpr uint8 MudPad = 3;
    constexpr uint8 FirstBurialLayer = 4;
    constexpr uint8 LastBurialLayer = 6;
    inline bool IsBurial(uint8 Brush) { return Brush >= FirstBurialLayer && Brush <= LastBurialLayer; }
    FIntVector QuantizeNormal(const FVector& Normal);
    bool IsBurialSurface(const FVector& FootAnchor, int32 CompletedLayers, const FVector& Point);
    FBox PartyBrushBounds(const FVoxelEdit& Edit);
    int32 PartyBrushDistance(const FVoxelEdit& Edit, FIntVector World);
    uint8 BrushFor(EVoxelAction Action);
    int32 RadiusFor(uint8 BrushId);
    ACTDEMO_API FIntVector QuantizeCenter(const FVector& Position);
    int32 ShoreDistance(int32 X, int32 Y);
    ACTDEMO_API int32 ShoreDistance(EVoxelLayout Layout, int32 X, int32 Y);
    ACTDEMO_API int32 BaseHeight(EVoxelLayout Layout, int32 X, int32 Y);
    ACTDEMO_API int32 ShoreExtent(EVoxelLayout Layout);
    ACTDEMO_API TArray<FVector2D> CoastOutline(EVoxelLayout Layout);
    ACTDEMO_API int32 IslandSurfaceHeight(int32 X, int32 Y);
}

struct FVoxelGrid
{
    FIntVector Origin;
    FIntVector Cells;

    static FVoxelGrid ForLayout(EVoxelLayout Layout);
    FIntVector Chunks() const { return Cells / VoxelPlayground::CellsPerChunk; }
    int32 NumChunks() const;
    FIntVector ChunkCoordinates(int32 Index) const;
    bool Contains(const FIntVector& Point) const;
    int32 SampleIndex(const FIntVector& Point) const;
    FIntVector SampleCoordinates(int32 Index) const;
    bool IsValidEdit(const FVoxelEdit& Edit) const;
};

struct FVoxelDensityPatch
{
    TMap<int32, int16> Samples;
    TArray<int32> Chunks;
};

/** Centimetres, with negative samples inside rock. Each edit is deterministic across peers. */
struct FVoxelDensity
{
    explicit FVoxelDensity(EVoxelLayout Layout);
    FVoxelDensityPatch PrepareEdit(const FVoxelEdit& Edit) const;
    FVoxelDensityPatch PrepareBrush(const FVoxelBrushStamp& Stamp) const;
    void Apply(const FVoxelDensityPatch& Patch);
    int16 Sample(const FIntVector& GridPoint, const FVoxelDensityPatch* Patch = nullptr) const;
    void CopySampleRow(FIntVector Start, int32 Count, int16* Destination) const;
    bool IsVolumeClear(const FBox& Box) const;
    bool IsVolumeSolid(const FBox& Box) const;
    ACTDEMO_API TOptional<double> SurfaceAt(FVector2D Position) const;
    ACTDEMO_API double SignedDistanceAt(FVector World) const;
    int32 ShoreDistance(int32 X, int32 Y) const { return VoxelPlayground::ShoreDistance(Layout, X, Y); }
    const TSet<int32>& EditedColumns() const { return PromotedColumns; }
    ACTDEMO_API uint32 Checksum() const;
    FString ChunkChecksums() const;

    const FVoxelGrid Grid;
    const EVoxelLayout Layout;

private:
    FVoxelDensityPatch PrepareStamp(const FVoxelBrushStamp& Stamp, const FVoxelEdit* PartyEdit, bool& bTouchesSolid) const;
    void ResolveSupport(FVoxelDensityPatch& Patch, bool bAdd, bool bTouchesSolid) const;
    bool VolumeMatches(const FBox& Box, bool bSolid) const;
    TArray<int16> Samples;
    // Columns in which the original terrain touches the world's foundation layer.
    TBitArray<> Foundation;
    // The expedition base is a cooked height surface. Retain only column boundaries and changed samples.
    TArray<FIntPoint> BaseColumns;
    // Only edited blocks allocate the full field. Meshing can then read a contiguous row at a time.
    TMap<int32, TArray<int16>> EditedBlocks;
    int32 BlockKey(const FIntVector& Point) const;
    TArray<int16>& GetOrCreateBlock(const FIntVector& Point);
    TSet<int32> PromotedColumns;
    uint32 SparseChecksum = 0;
    int16 BaseSample(const FIntVector& Point) const;
};
