#pragma once

#include "GameFramework/Actor.h"
#include "Island/IslandRules.h"
#include "Voxel/VoxelDensity.h"
#include "VoxelTerrain.generated.h"

class UProceduralMeshComponent;
class UStaticMesh;
class UInstancedStaticMeshComponent;
struct FHitResult;
struct FVoxelMeshBuild;

UCLASS()
class ACTDEMO_API AVoxelTerrain : public AActor
{
    GENERATED_BODY()

public:
    AVoxelTerrain();
    static AVoxelTerrain* Find(UWorld* World);
    int32 GetShoreExtent() const { return VoxelPlayground::ShoreExtent(Layout); }
    TArray<FVector2D> GetCoastOutline() const { return VoxelPlayground::CoastOutline(Layout); }
    bool IsInSea(const FVector& Head) const { return Head.Z < 0 && VoxelPlayground::ShoreDistance(Layout, FMath::RoundToInt(Head.X), FMath::RoundToInt(Head.Y)) >= 0; }

    bool IsReady() const { return bReady; }
    bool IsBusy() const { return bBuilding; }
    uint32 GetAppliedSequence() const { return AppliedSequence; }
    uint32 GetChecksum() const { return Density ? Density->Checksum() : 0; }
    uint32 GetInitialChecksum() const;
    const FVoxelDensity* GetDensity() const { return Density.Get(); }
    const TArray<FIslandSearchCircle>& GetSearchCircles() const { return SearchCircles; }
    FString GetChunkChecksums() const { return Density ? Density->ChunkChecksums() : FString(); }
    bool IsValidEdit(const FVoxelEdit& Edit) const { return Density && Density->Grid.IsValidEdit(Edit); }
    bool IsVolumeClear(const FBox& Box) const { return Density && Density->IsVolumeClear(Box); }

    UPROPERTY(EditAnywhere, Category = "Voxel") EVoxelLayout Layout = EVoxelLayout::Playground;
    bool TraceRock(const FVector& Start, const FVector& End, const AActor* Ignore, FHitResult& Hit) const;

    bool BuildEdit(const FVoxelEdit& Edit, TFunction<bool()> CanCommit, TFunction<void(bool)> Completion);

#if WITH_EDITOR
    void BakeExpeditionSurface();
    void RebuildEditorPreview();
    void ClearEditorPreview();
    bool TraceEditorPreview(const FVector& Start, const FVector& End, FHitResult& Hit) const;
    bool ApplyEditorStamps(TConstArrayView<FVoxelBrushStamp> Stamps);
    void SetSearchCircle(int32 Index, FIslandSearchCircle Circle);
    const FVoxelDensity* GetEditorDensity() const { return PreviewDensity.Get(); }
#endif

protected:
    void PostLoad() override;
    void BeginPlay() override;
    void Tick(float DeltaSeconds) override;

private:
    void StartBuild(FVoxelDensityPatch Patch, TOptional<FVoxelEdit> Edit,
        TFunction<bool()> CanCommit, TFunction<void(bool)> Completion);
    void Commit(FVoxelMeshBuild& Build);
    UProceduralMeshComponent* CreateChunk(int32 Index);
    void ConfigureChunk(UProceduralMeshComponent& Chunk);
    void SpawnFragments(const FVoxelEdit& Edit);

    UPROPERTY() TArray<FVoxelBrushStamp> AuthoredStamps;
    UPROPERTY() TArray<FIslandSearchCircle> SearchCircles;

#if WITH_EDITORONLY_DATA
    UPROPERTY(Transient, DuplicateTransient) TArray<TObjectPtr<UProceduralMeshComponent>> PreviewTiles;
    UPROPERTY(Transient, DuplicateTransient) TArray<TObjectPtr<UProceduralMeshComponent>> PreviewChunks;
#endif
#if WITH_EDITOR
    void RefreshPreviewChunk(int32 Index);
    void UpdatePreviewTile(int32 Tile);
    TSharedPtr<FVoxelDensity, ESPMode::ThreadSafe> PreviewDensity;
#endif

    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> MeshChunks;
    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> BakedTiles;
    UProceduralMeshComponent* MakeSurfaceComponent(bool bPersistent);
    UPROPERTY() TObjectPtr<UStaticMesh> FragmentMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> ExpeditionRockMaterial;

    struct FFragment
    {
        FTransform Transform;
        FVector Velocity;
        float Age = 0.f;
    };
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Debris;
    TArray<FFragment> Fragments;
    TArray<FTransform> DebrisTransforms;
    TSharedPtr<FVoxelDensity, ESPMode::ThreadSafe> Density;
    uint32 AppliedSequence = 0;
    uint64 ImmediateFrame = MAX_uint64;
    double ImmediateMs = 0.;
    bool bReady = false;
    bool bBuilding = false;
};
