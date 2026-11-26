#pragma once

#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "IslandFallEvent.h"
#include "IslandSceneryVoxels.generated.h"

struct FIslandMeshVoxelData;
class UIslandMeshVoxels;
class UStaticMesh;
class UMaterialInterface;
class AIslandPlayerController;



// The intact paint and collision surface are clipped to the same occupied cells when dug.
namespace IslandSceneryVoxels
{
    ACTDEMO_API void Register(AActor& Actor);
    ACTDEMO_API UPrimitiveComponent* SourceFor(UPrimitiveComponent* Surface);
    ACTDEMO_API UPrimitiveComponent* SurfaceFor(UPrimitiveComponent* Source);
    ACTDEMO_API bool IsExhausted(UPrimitiveComponent* Source);
    ACTDEMO_API void RefreshPart(UPrimitiveComponent& Source);
    ACTDEMO_API bool Strike(const FHitResult& Hit,float RadiusScale = 1.f);
    void PlayMechanismStrike(const FHitResult& Hit);
    ACTDEMO_API void CheckFoundations(UWorld& World,const FBox& Area);
}

UCLASS()
class ACTDEMO_API UIslandMeshVoxels : public UProceduralMeshComponent
{
    GENERATED_BODY()
public:
    UIslandMeshVoxels(const FObjectInitializer& Initializer = FObjectInitializer::Get());
    bool Initialize(UPrimitiveComponent& Source);
    UPrimitiveComponent* SourcePart() const { return Original; }
    void RefreshSource();
    int32 RemainingCount() const;
    int32 InitialCount() const;
    uint32 Digest() const;
    bool GetSurfaceTarget(FVector& Point,FVector& Normal) const;
    int32 GetImpactPaint(const FHitResult& Hit, FColor& VertexColor);
    bool IsSolidWorld(FVector World) const;
    double CellRadius() const;
    void GetCellAxes(FVector& A,FVector& B,FVector& C) const;
    FVector CellFoot(int32 Index) const;
    EIslandChipSurface CellSurface(int32 Index) const;
    bool FallingSections(TConstArrayView<uint8> Shape, TArray<FProcMeshSection>& Sections) const;
    void ForEachSolid(TFunctionRef<void(int32,FVector)> Visitor) const;
    void GetCellNeighbors(int32 Index,TArray<int32>& Out) const;
    void FindOccupiedNear(FVector World,double Radius,TArray<int32>& Out) const;
    bool RemoveCells(TConstArrayView<int32> Ids);
    bool Cut(FVector World,FVector Normal,float RadiusScale);
    void PackRemoved(TArray<uint8>& Data) const;
    void ApplyRemoved(const TArray<uint8>& Data);
private:
    UPROPERTY() TObjectPtr<UPrimitiveComponent> Original;
    TSharedPtr<FIslandMeshVoxelData> Field;
    void Rebuild();
    FCollisionResponseContainer SourceResponses;
    ECollisionEnabled::Type SourceCollision = ECollisionEnabled::NoCollision;
};

USTRUCT()
struct FIslandVoxelLoss
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<AActor> Owner;
    UPROPERTY() FName SourceName;
    UPROPERTY() TArray<uint8> Cells;
    UPROPERTY() uint32 Revision = 0;
};

UCLASS(NotPlaceable)
class ACTDEMO_API AIslandVoxelEdits : public AActor
{
    GENERATED_BODY()
public:
    AIslandVoxelEdits();
    void BeginPlay() override;
    static void Record(UIslandMeshVoxels& Mesh);
    static void Restore(UIslandMeshVoxels& Mesh);
    static void EmitChips(UWorld& World, const FHitResult& Hit, EIslandChipSurface Surface, FColor Color,
        FColor Alternate=FColor::Transparent);
    void PlayChips(AIslandPlayerController& Viewer, FVector Point, FVector Normal, EIslandChipSurface Surface,
        FColor Color, FColor Alternate) const;
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
private:
    UPROPERTY() TObjectPtr<UStaticMesh> StoneChip;
    UPROPERTY() TObjectPtr<UStaticMesh> Splinter;
    UPROPERTY() TObjectPtr<UStaticMesh> Scrap;
    UPROPERTY() TObjectPtr<UMaterialInterface> ChipFinish;
    UFUNCTION() void OnRep_Losses();
    UPROPERTY(ReplicatedUsing=OnRep_Losses) TArray<FIslandVoxelLoss> Losses;
    TMap<TWeakObjectPtr<UIslandMeshVoxels>,uint32> Applied;
    void Apply(UIslandMeshVoxels& Mesh,const FIslandVoxelLoss& Loss);
};
