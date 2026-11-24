#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "IslandRules.generated.h"

UENUM()
enum class EIslandPhase : uint8
{
    Waiting,
    Hunting,
    Finished,
    PartyLoading,
    PartyPlaying,
    PartyReturning
};

USTRUCT()
struct FIslandClues
{
    GENERATED_BODY()

    FIslandClues() { Points.SetNumZeroed(4); }

    UPROPERTY() uint8 Mask = 0;
    UPROPERTY() TArray<FVector2D> Points;
    UPROPERTY() FVector2D Crossing = FVector2D::ZeroVector;

    bool Complete() const { return Mask == 15; }
    int32 Count() const;
};

struct FVoxelDensity;

USTRUCT()
struct FIslandSearchCircle
{
    GENERATED_BODY()

    UPROPERTY() FIntPoint Center = FIntPoint::ZeroValue;
    UPROPERTY() int32 Radius = 300;
};

struct FIslandDecoy
{
    FVector Center;
    int32 ClueIndex;
};

struct FIslandTreasureLayout
{
    TStaticArray<FVector, 5> Centers;
    TArray<FIslandDecoy> Decoys;

    int32 Num() const { return Centers.Num() + Decoys.Num(); }
    bool IsDecoy(int32 Index) const { return Index >= Centers.Num(); }
    const FVector& GetCenter(int32 Index) const { return IsDecoy(Index) ? Decoys[Index - Centers.Num()].Center : Centers[Index]; }
    FVector2D Crossing = FVector2D::ZeroVector;
};

namespace IslandRules
{
    inline constexpr int32 Seats = 6;
    inline constexpr int32 TeamCount = 3;
    inline constexpr int32 ClueCount = 4;
    inline constexpr int32 PrizeIndex = ClueCount;
    inline constexpr int32 MaxDecoys = 24;
    inline constexpr int32 MinSearchRadius = 200;
    inline constexpr int32 MaxSearchRadius = 1000;
    inline constexpr double OpenReach = 200.0;
    inline constexpr double DrownTime = 3.0;
    inline constexpr double RespawnTime = 1.0;
    inline const FVector ChestExtent(45, 34, 32);

    ACTDEMO_API TArray<FIslandSearchCircle> DefaultSearchCircles();
    ACTDEMO_API FLinearColor TeamColor(int32 Team);
    ACTDEMO_API const TCHAR* TeamName(int32 Team);
    bool InSea(const FVector& Head);
    bool IsValidLayout(const FIslandTreasureLayout& Layout, TConstArrayView<FIslandSearchCircle> Circles,
        const FVoxelDensity& Density, FString* Error = nullptr);
    ACTDEMO_API bool GenerateLayout(int32 Seed, int32 DecoyCount, TConstArrayView<FIslandSearchCircle> Circles,
        const FVoxelDensity& Density, FIslandTreasureLayout& Layout, FString* Error = nullptr, bool bFixedOnly = false);
}
