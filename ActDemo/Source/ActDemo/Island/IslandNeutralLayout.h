#pragma once

#include "CoreMinimal.h"
#include "IslandNeutralLayout.generated.h"

class AVoxelTerrain;
struct FIslandTreasureLayout;

UENUM()
enum class EIslandNeutralKind : uint8
{
    Tree, Ship, Hopper, Sail, Tide
};

USTRUCT()
struct ACTDEMO_API FIslandNeutralPlacement
{
    GENERATED_BODY()
    UPROPERTY() EIslandNeutralKind Kind = EIslandNeutralKind::Tree;
    UPROPERTY() FVector Location = FVector::ZeroVector;
    UPROPERTY() int16 Yaw = 0;

    bool operator==(const FIslandNeutralPlacement& Other) const
    {
        return Kind == Other.Kind && Location == Other.Location && Yaw == Other.Yaw;
    }
};

USTRUCT()
struct ACTDEMO_API FIslandNeutralSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Neutral", meta=(ClampMin="0",ClampMax="100")) int32 TreeWeight = 4;
    UPROPERTY(EditAnywhere, Category="Neutral", meta=(ClampMin="0",ClampMax="100")) int32 ShipWeight = 4;
    UPROPERTY(EditAnywhere, Category="Neutral", meta=(ClampMin="0",ClampMax="100")) int32 HopperWeight = 4;
    UPROPERTY(EditAnywhere, Category="Neutral", meta=(ClampMin="0",ClampMax="100")) int32 SailWeight = 4;
    UPROPERTY(EditAnywhere, Category="Neutral", meta=(ClampMin="0",ClampMax="100")) int32 TideWeight = 4;
    UPROPERTY(EditAnywhere, Category="Neutral", meta=(ClampMin="0",Units="cm")) float MinimumSpacing = 1700.f;
    // World XY. Each box reserves its entire vertical column for the level author.
    UPROPERTY(EditAnywhere, Category="Neutral") TArray<FBox2D> ExcludedAreas;
};

namespace IslandNeutral
{
    // Construction coordinates describe the modules of each landmark. Vertical proportions
    // can differ from the ground plan so deck heights and controls remain within player reach.
    ACTDEMO_API FVector SiteScale(EIslandNeutralKind Kind);
    inline constexpr double TideTravel = 850.; // world centimetres from a mouth to its uphill exit
    inline constexpr double TideRadius = 210.;
    ACTDEMO_API void Generate(UWorld& World, const AVoxelTerrain& Terrain, int32 RoundSeed,
        const FIslandTreasureLayout& Treasure, TArray<FIslandNeutralPlacement>& Out);
#if !UE_BUILD_SHIPPING
    ACTDEMO_API bool Audit(UWorld& World, const AVoxelTerrain& Terrain, int32 DecoyCount);
#endif
}
