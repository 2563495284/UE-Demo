#pragma once

#include "GameFramework/Actor.h"
#include "Island/IslandScenerySupport.h"
#include "Island/IslandSceneryVoxels.h"
#include "IslandFallingScenery.generated.h"

class UIslandRouteVoxelComponent;
class UProceduralMeshComponent;
class UMaterialInterface;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;

// Shape is independent of the live field, which can already have received a later edit.
struct FIslandLooseSamples
{
    TArray<uint8> Shape;
    TArray<FVector> Feet;
};

UCLASS(NotPlaceable, Transient)
class ACTDEMO_API AIslandFallingScenery : public AActor
{
    GENERATED_BODY()
public:
    AIslandFallingScenery();
    static void Release(AActor& Subject, const TArray<IslandScenerySupport::FCellGroup>& Groups);
    static void Release(UIslandRouteVoxelComponent& Field, const TArray<FIslandLooseSamples>& Groups, EIslandChipSurface Surface);
    static void PackCells(TArray<int32> Cells, TArray<uint8>& Out);
    static bool UnpackCells(TConstArrayView<uint8> Data, int32 Limit, TArray<int32>& Out);
    void Tick(float DeltaSeconds) override;


private:
    friend class FIslandCrossPartSupportTest;
    friend class AIslandPlayerController;
    UPROPERTY() FIslandSceneryFall Fall;
    UPROPERTY() FIslandFallContact Contact;
    UPROPERTY() TObjectPtr<UMaterialInterface> DustBase;
    UPROPERTY() TObjectPtr<UStaticMesh> DustPlane;
    UPROPERTY() TObjectPtr<UStaticMesh> PuffShape;
    UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> DustPuffs;
    TArray<FTransform> PuffPoses;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Dust;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DustFinish;
    FLinearColor DustTint;
    TArray<FVector> ContactFeet;
    double NextContactCheck = 0.;
    double VisibleAt = 0.;
    double ImpactAt = 0.;
    void CheckLanding(double Age);
    void DrawDust(double After);
    UPROPERTY(Transient) TArray<TObjectPtr<UProceduralMeshComponent>> Pieces;
    UPROPERTY() TObjectPtr<UMaterialInterface> WaterPaint;
    UPROPERTY() TObjectPtr<UStaticMesh> DropShape;
    UPROPERTY() TObjectPtr<UStaticMesh> ReefShape;
    UPROPERTY() TObjectPtr<UStaticMesh> FoamShape;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Foam;
    TArray<int32> PieceIndices;
    bool bReportedSourceWait = false;
    bool bTracedMotion = false;
    bool bTracedDust = false;
    bool bShown = false;
    bool bHit = false;

    static void Spawn(AActor& Subject, TArray<FIslandFallingPart>&& Parts, const TArray<FVector>& Feet, EIslandChipSurface Surface);
    bool CreatePieces();
    void Impact();
};
