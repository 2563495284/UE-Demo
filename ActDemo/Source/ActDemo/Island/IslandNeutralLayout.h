#pragma once

#include "CoreMinimal.h"
#include "IslandNeutralLayout.generated.h"

class AVoxelTerrain;
namespace NeutralLayoutDetail { struct FIslandSurface; }
struct FIslandTreasureLayout;

UENUM()
enum class EIslandNeutralKind : uint8
{
    Tree, Ship, Hopper, Sail, Tide, Bridge, Dam, SandDepot, WindTower
};

USTRUCT()
struct ACTDEMO_API FIslandNeutralPlacement
{
    GENERATED_BODY()
    UPROPERTY() EIslandNeutralKind Kind = EIslandNeutralKind::Tree;
    UPROPERTY() FVector Location = FVector::ZeroVector;
    UPROPERTY() int16 Yaw = 0;
    // Ground points in world centimetres, ordered from the station to the far search area.
    UPROPERTY() TArray<FVector> RouteNodes;

    bool operator==(const FIslandNeutralPlacement& Other) const
    {
        return Kind == Other.Kind && Location == Other.Location && Yaw == Other.Yaw && RouteNodes == Other.RouteNodes;
    }
};

USTRUCT()
struct ACTDEMO_API FIslandNeutralSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, Category="Neutral") bool bBridge = true;
    UPROPERTY(EditAnywhere, Category="Neutral") bool bDam = true;
    UPROPERTY(EditAnywhere, Category="Neutral") bool bSandDepot = true;
    UPROPERTY(EditAnywhere, Category="Neutral") bool bWindTower = true;
    UPROPERTY(EditAnywhere, Category="Neutral", meta=(ClampMin="0",Units="cm")) float MinimumSpacing = 1700.f;
    // World XY. Each box reserves its entire vertical column for the level author.
    UPROPERTY(EditAnywhere, Category="Neutral") TArray<FBox2D> ExcludedAreas;
};

namespace IslandNeutral
{
    // Construction coordinates describe the modules of each landmark. Vertical proportions
    // can differ from the ground plan so deck heights and controls remain within player reach.
    ACTDEMO_API FVector SiteScale(EIslandNeutralKind Kind);
    inline constexpr double TideTravel = 1350.; // world centimetres from a mouth to its uphill exit
    inline constexpr double TideRadius = 305.;
    using FSurface = TSharedPtr<const NeutralLayoutDetail::FIslandSurface>;
    FSurface Survey(UWorld& World, const AVoxelTerrain& Terrain);
    bool CanReachDigSite(const FSurface& Surface, FVector2D Point);
    inline constexpr int32 RouteSegments = 24;
    inline constexpr double MinRouteSpan = 5500.;
    inline constexpr double MaxRouteLength = 7500.;
    // Separate solid obstacles for navigation from the complete corridor reserved for scenery.
    void AppendFootprints(const FIslandNeutralPlacement& Placement, TArray<FBox2D>& Out, bool bEntireCorridor = false);
    bool KeepsDigRoutes(const FSurface& Surface, const FIslandTreasureLayout& Treasure,
        TConstArrayView<FBox2D> Footprints);
    void Generate(const FSurface& Surface, int32 RoundSeed,
        const FIslandTreasureLayout& Treasure, TArray<FIslandNeutralPlacement>& Out);
#if !UE_BUILD_SHIPPING
    bool Audit(const FSurface& Surface, int32 DecoyCount, TConstArrayView<FBox2D> Obstacles);
#endif
}
