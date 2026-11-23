#pragma once

#include "GameFramework/Actor.h"
#include "Voxel/VoxelDensity.h"
#include "VoxelTerrain.generated.h"

class UProceduralMeshComponent;
class UStaticMesh;
struct FHitResult;
struct FVoxelMeshBuild;

UCLASS()
class ACTDEMO_API AVoxelTerrain : public AActor
{
    GENERATED_BODY()

public:
    AVoxelTerrain();
    static AVoxelTerrain* Find(UWorld* World);

    bool IsReady() const { return bReady; }
    bool IsBusy() const { return bBuilding; }
    uint32 GetAppliedSequence() const { return AppliedSequence; }
    uint32 GetChecksum() const { return Density ? Density->Checksum() : 0; }
    FString GetChunkChecksums() const { return Density ? Density->ChunkChecksums() : FString(); }
    bool IsValidEdit(const FVoxelEdit& Edit) const { return Density && Density->Grid.IsValidEdit(Edit); }
    bool IsVolumeClear(const FBox& Box) const { return Density && Density->IsVolumeClear(Box); }

    UPROPERTY(EditAnywhere, Category = "Voxel") EVoxelLayout Layout = EVoxelLayout::Playground;
    bool TraceRock(const FVector& Start, const FVector& End, const AActor* Ignore, FHitResult& Hit) const;

    bool BuildEdit(const FVoxelEdit& Edit, TFunction<bool()> CanCommit, TFunction<void(bool)> Completion);

protected:
    void BeginPlay() override;
    void Tick(float DeltaSeconds) override;

private:
    void StartBuild(FVoxelDensityPatch Patch, TOptional<FVoxelEdit> Edit,
        TFunction<bool()> CanCommit, TFunction<void(bool)> Completion);
    void Commit(FVoxelMeshBuild& Build);
    UProceduralMeshComponent* CreateChunk(int32 Index);
    void ConfigureChunk(UProceduralMeshComponent& Chunk);
    void SpawnFragments(const FVoxelEdit& Edit);

    UPROPERTY() TArray<TObjectPtr<UProceduralMeshComponent>> MeshChunks;
    UPROPERTY() TObjectPtr<UStaticMesh> FragmentMesh;

    struct FFragment
    {
        TWeakObjectPtr<UStaticMeshComponent> Mesh;
        FVector Velocity;
        float Age = 0.f;
    };
    TArray<FFragment> Fragments;
    TSharedPtr<FVoxelDensity, ESPMode::ThreadSafe> Density;
    uint32 AppliedSequence = 0;
    bool bReady = false;
    bool bBuilding = false;
};
