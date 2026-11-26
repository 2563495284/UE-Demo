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
    PartyReturning,
    Starting
};

UENUM()
enum class EIslandDecoyEvent : uint8
{
    None, WideTools, Scatter, Disguise, SpringTide, Ceasefire, CrewRally, SandRamp, SailGust
};

USTRUCT()
struct FIslandClues
{
    GENERATED_BODY()

    FIslandClues() { Points.SetNumZeroed(4); }

    UPROPERTY() uint8 Mask = 0;
    // Known exact positions still available for the crew to excavate and claim.
    UPROPERTY() uint8 ScoutedMask = 0;
    UPROPERTY() TArray<FVector2D> Points;
    UPROPERTY() FVector2D Crossing = FVector2D::ZeroVector;
    UPROPERTY() bool bUsesKeyFragments = false;
    UPROPERTY() bool bKeySpent = false;

    bool Complete() const { return Mask == 15; }
    static constexpr int32 FragmentsPerKey = 3;
    int32 FragmentCount() const { return bUsesKeyFragments ? Count() % FragmentsPerKey : 0; }
    bool HasKey() const { return bUsesKeyFragments && Count() >= FragmentsPerKey && !bKeySpent; }
    bool CanOpenPrize() const { return Complete() && (!bUsesKeyFragments || HasKey()); }
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
    inline constexpr int32 CharacterModelCount = 4;
    inline constexpr bool IsCharacterModel(int32 Index) { return Index >= 0 && Index < CharacterModelCount; }
    inline constexpr int32 ClueCount = 4;
    inline constexpr int32 PrizeIndex = ClueCount;
    inline constexpr int32 MaxDecoys = 24;
    inline constexpr int32 MinSearchRadius = 200;
    inline constexpr int32 MaxSearchRadius = 1000;
    inline constexpr int32 ExpeditionSearchRadius = 1800;
    inline constexpr int32 MaxExpeditionSearchRadius = 2500;
    inline constexpr double OpenReach = 200.0;
    inline constexpr double LockSeconds = 4.0;
    inline constexpr double DrownTime = 3.0;
    inline constexpr double RespawnTime = 1.0;
    inline constexpr float SpringTideJumpScale = 1.5f;
    inline const FVector ChestExtent(45, 34, 32);

    ACTDEMO_API TArray<FIslandSearchCircle> DefaultSearchCircles();
    inline constexpr int32 DecoyEventBit(EIslandDecoyEvent Event) { return 1 << int32(Event); }
    inline bool IsPersistentDecoyEvent(EIslandDecoyEvent Event)
    {
        return Event == EIslandDecoyEvent::WideTools || Event == EIslandDecoyEvent::Disguise
            || Event == EIslandDecoyEvent::SpringTide || Event == EIslandDecoyEvent::Ceasefire;
    }
    ACTDEMO_API EIslandDecoyEvent DrawDecoyEvent(int32 IslandSeed, int32 ChestIndex, int32 PlayerCount,
        int32 ParticipatingTeamMask, int32 WinnerMask, int32 WorldEventMask = 0);
    ACTDEMO_API FLinearColor TeamColor(int32 Team);
    ACTDEMO_API const TCHAR* TeamName(int32 Team);
    bool InSea(const FVector& Head);
    ACTDEMO_API TArray<FBox2D> CollectSearchObstacles(UWorld& World);
#if !UE_BUILD_SHIPPING
    bool AuditSearchAreas(int32 DecoyCount, TConstArrayView<FIslandSearchCircle> Circles, const FVoxelDensity& Density, TConstArrayView<FBox2D> Obstacles,
        const TFunction<bool(FVector2D)>& CanReachDecoy);
#endif
    bool IsValidLayout(const FIslandTreasureLayout& Layout, TConstArrayView<FIslandSearchCircle> Circles,
        const FVoxelDensity& Density, FString* Error = nullptr, TConstArrayView<FBox2D> Obstacles = {});
    ACTDEMO_API bool GenerateLayout(int32 Seed, int32 DecoyCount, TConstArrayView<FIslandSearchCircle> Circles,
        const FVoxelDensity& Density, FIslandTreasureLayout& Layout, FString* Error = nullptr, bool bFixedOnly = false,
        TConstArrayView<FBox2D> Obstacles = {}, const TFunction<bool(FVector2D)>& CanReachDecoy = {});
}
