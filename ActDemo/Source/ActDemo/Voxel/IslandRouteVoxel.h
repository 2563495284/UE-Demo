#pragma once

#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "Voxel/IslandRouteFlora.h"
#include "IslandRouteVoxel.generated.h"

class AVoxelTerrain;

struct FRouteVoxelSample
{
    double Distance = 0.;
    FColor Color = FColor::White;
};

// Local coordinates and spacing are centimetres. Render triangles and walking collision are
// extracted together from the same signed samples. The owning actor replicates its edits.
UCLASS()
class ACTDEMO_API UIslandRouteVoxelComponent : public UProceduralMeshComponent
{
    GENERATED_BODY()

public:
    UIslandRouteVoxelComponent(const FObjectInitializer& Initializer = FObjectInitializer::Get());
    bool InitializeField(const FBox& FieldBounds, double Spacing,
        TFunctionRef<FRouteVoxelSample(FVector)> SampleShape);
    bool ApplyWorldCut(FVector Point, FVector OutwardNormal, double Radius = 62.);
    // Test each occupied local sample against the owner's real attachment/support.
    bool CullUnsupported(TFunctionRef<bool(FVector)> IsLocalAnchor);
    bool IsSolidLocal(FVector Local) const;
    bool IsSolidWorld(FVector World) const;
    int32 RemainingCount() const { return SolidCount; }
    int32 InitialCount() const { return InitialSolidCount; }
    uint32 Digest() const;
    FBox GetFieldLocalBounds() const;
    void WriteChanges(TArray<uint8>& Out) const;
    void ReadChanges(const TArray<uint8>& Changes);
    double CellSpacing() const { return Step; }

private:
    FVector GridStart = FVector::ZeroVector;
    FIntVector Size = FIntVector::ZeroValue;
    double Step = 0.;
    TArray<int8> Samples, Original;
    TArray<FColor> Pigments;
    int32 SolidCount = 0, InitialSolidCount = 0;
    int32 Index(FIntVector P) const { return P.X + Size.X * (P.Y + Size.Y * P.Z); }
    FIntVector Coordinates(int32 I) const;
    bool Inside(FIntVector P) const;
    int8 At(FIntVector P) const;
    FColor SurfaceColor(FVector Local) const;
    void UpdateSurface();
};

USTRUCT()
struct FIslandRouteVoxelState
{
    GENERATED_BODY()
    UPROPERTY() uint8 Kind = 255;
    UPROPERTY() int32 Variant = 0;
    UPROPERTY() TArray<uint8> Changes;
};

UCLASS(NotPlaceable)
class ACTDEMO_API AIslandRouteVoxel : public AActor
{
    GENERATED_BODY()

public:
    AIslandRouteVoxel();
    static AIslandRouteVoxel* Spawn(UWorld& World, IslandRouteFlora::EShape Kind, FVector Location,
        int32 Yaw, int32 Variant);
    bool Strike(const FHitResult& Hit, bool bWide = false);
    UIslandRouteVoxelComponent* Field() const { return Voxels; }
    IslandRouteFlora::EShape GetKind() const { return static_cast<IslandRouteFlora::EShape>(State.Kind); }
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    bool RefreshGrounding(const AVoxelTerrain& Terrain);

private:
    UPROPERTY() TObjectPtr<UIslandRouteVoxelComponent> Voxels;
    UPROPERTY(ReplicatedUsing=OnRep_State) FIslandRouteVoxelState State;
    UFUNCTION() void OnRep_State();
    void Publish();
    void BeginPlay() override;
};
