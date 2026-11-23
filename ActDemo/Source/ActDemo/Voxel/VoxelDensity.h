#pragma once

#include "CoreMinimal.h"
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
    Island
};

USTRUCT()
struct FVoxelEdit
{
    GENERATED_BODY()

    UPROPERTY() uint32 Sequence = 0;
    UPROPERTY() EVoxelAction Action = EVoxelAction::Punch;
    UPROPERTY() FIntVector Center = FIntVector::ZeroValue;
    UPROPERTY() uint8 BrushId = 0;
};

namespace VoxelPlayground
{
    inline constexpr int32 Spacing = 25;
    inline constexpr int32 CellsPerChunk = 16;
    inline constexpr int32 ChunkCount = 24;

    uint8 BrushFor(EVoxelAction Action);
    int32 RadiusFor(uint8 BrushId);
    FIntVector QuantizeCenter(const FVector& Position);
    int32 ShoreDistance(int32 X, int32 Y);
    int32 IslandSurfaceHeight(int32 X, int32 Y);
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
    void Apply(const FVoxelDensityPatch& Patch);
    int16 Sample(const FIntVector& GridPoint, const FVoxelDensityPatch* Patch = nullptr) const;
    bool IsVolumeClear(const FBox& Box) const;
    uint32 Checksum() const;
    FString ChunkChecksums() const;

    const FVoxelGrid Grid;
    const EVoxelLayout Layout;

private:
    TArray<int16> Samples;
};
