#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "IslandRules.generated.h"

UENUM()
enum class EIslandPhase : uint8
{
    Waiting,
    Hunting,
    Finished
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

struct FIslandTreasureLayout
{
    TStaticArray<FVector, 5> Centers;
    FVector2D Crossing = FVector2D::ZeroVector;
};

namespace IslandRules
{
    inline constexpr int32 Seats = 6;
    inline constexpr int32 TeamCount = 3;
    inline constexpr int32 ClueCount = 4;
    inline constexpr int32 PrizeIndex = 4;
    inline constexpr double SearchRadius = 300.0;
    inline constexpr double OpenReach = 200.0;
    inline constexpr double DrownTime = 3.0;
    inline constexpr double RespawnTime = 1.0;
    inline const FVector ChestExtent(45, 34, 32);

    FVector2D SearchCenter(int32 Index);
    ACTDEMO_API FLinearColor TeamColor(int32 Team);
    ACTDEMO_API const TCHAR* TeamName(int32 Team);
    bool InSea(const FVector& Head);
    bool IsValidLayout(const FIslandTreasureLayout& Layout);
    FIslandTreasureLayout GenerateLayout(int32 Seed);
}
