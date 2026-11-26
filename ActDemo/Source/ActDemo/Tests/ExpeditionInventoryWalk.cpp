#include "Tests/ExpeditionWalk.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandBreakable.h"
#include "Island/IslandWorld.h"
#include "Voxel/IslandRouteVoxel.h"
#include "Voxel/VoxelTerrain.h"

bool AExpeditionWalk::FrameLandmark(int32 Index, AVoxelTerrain& Terrain, FVector& Focus, FVector& Eye, float& Fov) const
{
    // Offsets are measured in each landmark's frame so the view follows a rotated island.
    static const struct { FVector Observer; double AimHeight; } Views[] = {
        {{-915.,275.,240.},375.}, {{1150.,290.,310.},535.},
        {{-690.,-765.,390.},360.}, {{-1060.,165.,295.},445.}
    };
    if (Index < 0 || Index >= UE_ARRAY_COUNT(Views)) return false;
    for (TActorIterator<AExpeditionLandmark> It(GetWorld()); It; ++It)
    {
        if (It->ClueIndex != Index) continue;
        const FTransform& Frame = It->GetActorTransform();
        const auto& View = Views[Index];
        Focus = Frame.TransformPositionNoScale(FVector(0.,0.,View.AimHeight));
        const FVector NearEye = Frame.TransformPositionNoScale(View.Observer);
        Fov = 66.f;
        Eye = Focus + (NearEye-Focus)/FMath::Tan(FMath::DegreesToRadians(Fov*.5f));
        Eye.Z = FMath::Max(Eye.Z,Terrain.GetDensity()->SurfaceAt(FVector2D(Eye)).Get(Eye.Z-110.)+110.);
        return true;
    }
    return false;
}

bool AExpeditionWalk::FrameInventory(AVoxelTerrain& Terrain,const FVector& Sun,FVector& Focus,FVector& Eye,float& Fov)
{
    if (ArtShot >= 8) return FrameLandmark(ArtShot-8,Terrain,Focus,Eye,Fov);
    AActor* Subject=nullptr;
    if (ArtShot == 0)
    {
        for (TActorIterator<AExpeditionDock> It(GetWorld()); It; ++It)
            if (It->DockIndex==0) { Subject=*It; break; }
    }
    else if (ArtShot <= 2)
    {
        const FName Needed = ArtShot==1 ? FName(TEXT("SM_Cairn")) : FName(TEXT("SM_TrailBeacon"));
        for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
        {
            const UStaticMesh* Shape=It->GetStaticMeshComponent()->GetStaticMesh();
            if (Shape && Shape->GetFName()==Needed && (!Subject || It->GetActorLocation().X<Subject->GetActorLocation().X)) Subject=*It;
        }
    }
    else if (ArtShot <= 4)
    {
        const auto Kind=ArtShot==3 ? IslandRouteFlora::EShape::Saltbush : IslandRouteFlora::EShape::CoralSpur;
        for (TActorIterator<AIslandRouteVoxel> It(GetWorld()); It; ++It)
            if (It->GetKind()==Kind && (!Subject || It->GetActorLocation().SizeSquared2D()<Subject->GetActorLocation().SizeSquared2D())) Subject=*It;
    }
    else if (ArtShot <= 6)
    {
        // Evidence for normally buried prizes: place the real gameplay actor in
        // daylight and run its ordinary reveal. This mode has its own disposable match.
        if (!ArtChest)
        {
            FVector Ground(180.,-320.,0.);
            Ground.Z=Terrain.GetDensity()->SurfaceAt(FVector2D(Ground)).Get(0.)+32.;
            ArtChest=GetWorld()->SpawnActor<AIslandChest>(Ground,FRotator(0.,Sun.Rotation().Yaw-90.,0.));
        }
        if (!ArtChest) return false;
        if (ArtShot==6)
        {
            ArtChest->SetPrize(true);
            ArtChest->RevealPrize(FMath::Max(.01,GetWorld()->GetGameState()->GetServerWorldTimeSeconds()-3.));
        }
        const FVector Front=ArtChest->GetActorRightVector(), Side=ArtChest->GetActorForwardVector();
        Focus=ArtChest->GetActorLocation()+FVector(0,0,ArtShot==5 ? -2. : 42.);
        Eye=Focus+(Front*(ArtShot==5 ? 1. : .70)+Side*(ArtShot==5 ? .43 : 1.)).GetSafeNormal()*(ArtShot==5 ? 211. : 420.)+FVector(0,0,ArtShot==5 ? 94. : 113.);
        Fov=47.f;
        return true;
    }
    else if (ArtShot==7)
    {
        // The same floating actor normally appears only during a water rescue.
        Subject=GetWorld()->SpawnActor<AIslandRescueRaft>(FVector(10100.,0.,65.),FRotator(0,Sun.Rotation().Yaw-27.,0));
    }
    if (!Subject) return false;
    FVector Extent;
    Subject->GetActorBounds(false,Focus,Extent);
    const double Distance=FMath::Max(Extent.Size2D(),Extent.Z*1.58)*(ArtShot==0 ? 2.25 : 2.5);
    Eye=Focus+Sun*Distance+FVector(0,0,Distance*(ArtShot==7 ? .68 : .45));
    Eye.Z=FMath::Max(Eye.Z,Terrain.GetDensity()->SurfaceAt(FVector2D(Eye)).Get(Eye.Z-65.)+65.);
    Fov=55.f;
    return true;
}
