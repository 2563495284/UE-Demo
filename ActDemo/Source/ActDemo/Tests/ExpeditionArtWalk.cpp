#include "Tests/ExpeditionWalk.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandSceneryVoxels.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/IslandRouteVoxel.h"
#include "Voxel/VoxelTerrain.h"
#include "UnrealClient.h"

// A photo pass through a normally seeded match. Only the observer camera moves.
void AExpeditionWalk::TickArtTour()
{
    static const TCHAR* Shots[] = {TEXT("Island"),TEXT("CrewAtDock"),TEXT("Tree"),TEXT("Ship"),
        TEXT("Hopper"),TEXT("Sail"),TEXT("Tide"),TEXT("LandmarkA"),TEXT("LandmarkB"),TEXT("LandmarkC"),TEXT("LandmarkD"),
        TEXT("TrailCanopy"),TEXT("TrailReef"),TEXT("TrailAtEyeLevel"),TEXT("TrailCenter"),
        TEXT("ReefBeforeCut"),TEXT("ReefAfterCut"),TEXT("ArchBeforeCut"),TEXT("ArchAfterCut")};
    if (FScreenshotRequest::IsScreenshotRequested()) return;
    static const TCHAR* WreckShots[] = {TEXT("WreckShore"),TEXT("WreckProw"),TEXT("WreckCanvas"),
        TEXT("WreckSeaward"),TEXT("WreckPlank"),TEXT("WreckPlankCut")};
    const bool bWreckStudy = FParse::Param(FCommandLine::Get(),TEXT("WreckStudyPhotos"));
    static const TCHAR* InventoryShots[]={TEXT("Dock"),TEXT("Cairn"),TEXT("TrailBeacon"),TEXT("Saltbush"),
        TEXT("CoralSpur"),TEXT("ClueChest"),TEXT("PrizeCompass"),TEXT("RescueRaft"),
        TEXT("ArchQuarter"),TEXT("SignalQuarter"),TEXT("QuarryQuarter"),TEXT("FinQuarter")};
    const bool bInventory=FParse::Param(FCommandLine::Get(),TEXT("IslandInventoryPhotos"));
    const bool bTour=GetNetMode()==NM_Standalone && FParse::Param(FCommandLine::Get(),TEXT("IslandArtPreview"));
    const int32 ShotCount = bInventory ? UE_ARRAY_COUNT(InventoryShots) : bWreckStudy ? UE_ARRAY_COUNT(WreckShots) :
        (FParse::Param(FCommandLine::Get(),TEXT("RouteCarvingPhotos")) ? UE_ARRAY_COUNT(Shots) : UE_ARRAY_COUNT(Shots)-4);
    if (!bTour && ArtShot == ShotCount)
    {
        UE_LOG(LogTemp,Display,TEXT("EXPEDITION_ART_DONE captured=%d"),ArtCaptured);
        FPlatformMisc::RequestExitWithStatus(false,0);
        return;
    }
    auto* Player = Cast<AIslandPlayerController>(GetWorld()->GetFirstPlayerController());
    auto* State = GetWorld()->GetGameState<AIslandGameState>();
    auto* Terrain = AVoxelTerrain::Find(GetWorld());
    if (!Player || !Player->GetHero() || !State || !Terrain || !Terrain->IsReady()) return;
    if (!bStarted && State->Phase == EIslandPhase::Waiting && State->CanLaunch())
    {
        Player->ServerStartExpedition();
        bStarted = true;
    }
    if (State->Phase != EIslandPhase::Hunting || !Player->IsPlaying()) return;
    if (bTour && (ArtShot>=ShotCount || Player->WasInputKeyJustPressed(EKeys::SpaceBar)))
    {
        if (AHUD* Hud=Player->GetHUD()) Hud->bShowHUD=true;
        Player->SetViewTarget(Player->GetHero());
        if (ArtCamera) ArtCamera->Destroy();
        UE_LOG(LogTemp,Display,TEXT("ISLAND_ART_PREVIEW_PLAYING after_views=%d pawn_camera=%d hud=%d"),ArtShot,
            Player->GetViewTarget()==Player->GetHero(),Player->GetHUD() && Player->GetHUD()->bShowHUD);
        Destroy();
        return;
    }
    if (bArtFramed)
    {
        if (Player->GetViewTarget() != ArtCamera)
        {
            Player->SetViewTarget(ArtCamera);
            ArtCameraFrame = GFrameCounter;
            return;
        }
        if (bTour)
        {
            if (GetWorld()->GetRealTimeSeconds()-TourViewSince >= 2.9)
            {
                ++ArtShot;
                bArtFramed=false;
            }
            return;
        }
        if (GFrameCounter-ArtCameraFrame < (bInventory ? 38 : 20)) return;
        const FString File = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
            TEXT("ExpeditionArt"), FString(bInventory ? InventoryShots[ArtShot] : bWreckStudy ? WreckShots[ArtShot] : Shots[ArtShot]) + TEXT(".png")));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
        FScreenshotRequest::RequestScreenshot(File,false,false);
        ++ArtCaptured;
        ++ArtShot;
        bArtFramed = false;
        return;
    }
    if (!ArtCamera)
    {
        ArtCamera = GetWorld()->SpawnActor<ACameraActor>();
        if (AHUD* Hud = Player->GetHUD()) Hud->bShowHUD = false;
    }
    FVector Focus, Eye;
    TActorIterator<ADirectionalLight> Light(GetWorld());
    const FVector Sun = Light ? -Light->GetActorForwardVector().GetSafeNormal2D() : FVector(-.8,.6,0.);
    float Fov = 58.f;
    if (bInventory)
    {
        if (!FrameInventory(*Terrain,Sun,Focus,Eye,Fov))
        {
            UE_LOG(LogTemp,Error,TEXT("Missing island inventory photo subject: %s"),InventoryShots[ArtShot]);
            FPlatformMisc::RequestExitWithStatus(false,1); return;
        }
    }
    else if (bTour && ArtShot >= 7 && ArtShot <= 10)
    {
        if (!FrameLandmark(ArtShot-7,*Terrain,Focus,Eye,Fov)) { ++ArtShot; return; }
    }
    else if (bWreckStudy)
    {
        AIslandNeutralProp* Ship = nullptr;
        for (TActorIterator<AIslandNeutralProp> It(GetWorld()); It; ++It)
            if (It->GetKind() == EIslandNeutralKind::Ship && (!Ship || It->GetActorLocation().X < Ship->GetActorLocation().X)) Ship = *It;
        if (!Ship) { StopRun(TEXT("Wreck study requires a round containing a ship")); FPlatformMisc::RequestExitWithStatus(false,1); return; }
        // Distances in world centimetres. Fitting to mesh bounds would move the camera when a sail changes size.
        const FVector X = Ship->GetActorForwardVector();
        const FVector Shore = FVector::DotProduct(X,Sun) < 0. ? -X : X;
        const FVector Keel = Ship->GetActorRightVector();
        static const struct { double AimAlong, AimUp, Offshore, Along, Up, Fov; } Views[] = {
            {-10.,190., 2650.,-1160.,970.,57.},
            {-930.,205., 1100.,-1030.,430.,55.},
            {625.,335., 1380.,650.,420.,57.},
            {-35.,210., -2670.,-1510.,490.,55.},
            {-610.,80., 835.,-155.,135.,53.},
            {-610.,80., 835.,-155.,135.,53.}
        };
        static_assert(UE_ARRAY_COUNT(Views) == UE_ARRAY_COUNT(WreckShots));
        const auto& View = Views[ArtShot];
        Focus = Ship->GetActorLocation() + Keel * View.AimAlong + FVector(0,0,View.AimUp);
        Eye = Focus + Shore * View.Offshore + Keel * View.Along + FVector(0,0,View.Up);
        Eye.Z = FMath::Max(Eye.Z,Terrain->GetDensity()->SurfaceAt(FVector2D(Eye)).Get(Eye.Z-145.)+145.);
        Fov = View.Fov;
        if (ArtShot >= 4)
        {
            const FVector Direction = (Focus-Eye).GetSafeNormal();
            auto* Hero = Player->GetHero();
            FHitResult Hit;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(WreckPhoto),false,Hero);
            GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Direction*1800.,ECC_Visibility,Query);
            auto* Plank = Cast<UIslandMeshVoxels>(Hit.GetComponent());
            if (!Plank || Plank->GetOwner() != Ship)
            {
                UE_LOG(LogTemp,Error,TEXT("WRECK_STUDY_SHOVEL could not see the hull; hit=%s"),*GetNameSafe(Hit.GetComponent()));
                FPlatformMisc::RequestExitWithStatus(false,1);
                return;
            }
            if (ArtShot == 5)
            {
                Hero->SetActorHiddenInGame(true);
                Hero->SetActorLocation(Hit.ImpactPoint - Direction*95. - (Hero->GetAimOrigin()-Hero->GetActorLocation()),false,nullptr,ETeleportType::TeleportPhysics);
                const int32 Before = Plank->RemainingCount();
                Player->SubmitEdit(EVoxelAction::Punch,Direction);
                const int32 After = Plank->RemainingCount();
                UE_LOG(LogTemp,Display,TEXT("WRECK_STUDY_SHOVEL part=%s before=%d after=%d"),*Plank->SourcePart()->GetName(),Before,After);
                if (Before <= After || After == 0) { FPlatformMisc::RequestExitWithStatus(false,1); return; }
            }
        }
        UE_LOG(LogTemp,Display,TEXT("WRECK_STUDY_VIEW %s eye=%s focus=%s fov=%.1f"),WreckShots[ArtShot],*Eye.ToString(),*Focus.ToString(),Fov);
    }
    else if (ArtShot == 0)
    {
        Focus = FVector(0,0,220);
        Eye = Focus + Sun * 13800. + FVector(0,0,9200);
        Fov = 63.f;
    }
    else if (ArtShot == 1)
    {
        auto* Hero = Player->GetHero();
        Focus = Hero->GetActorLocation() + FVector(0,0,7);
        Eye = Focus + Hero->GetActorForwardVector()*333. + Hero->GetActorRightVector()*158. + FVector(0,0,74);
        Fov = 49.f;
    }
    else if (ArtShot>=15)
    {
        const bool bRock=ArtShot<17;
        UPrimitiveComponent* Target=nullptr;
        if(bRock)
        {
            AIslandRouteVoxel* Near=nullptr;
            for(TActorIterator<AIslandRouteVoxel> It(GetWorld());It;++It)
                if(It->GetKind()==IslandRouteFlora::EShape::SplitReef && (!Near || It->GetActorLocation().SizeSquared2D()<Near->GetActorLocation().SizeSquared2D())) Near=*It;
            if(Near) Target=Near->Field();
        }
        else for(TActorIterator<AExpeditionLandmark> It(GetWorld());It && !Target;++It)
        {
            if(It->ClueIndex!=0) continue;
            TInlineComponentArray<UIslandMeshVoxels*> Fields(*It);
            for(auto* Field:Fields) if(Field->SourcePart()->GetName().StartsWith(TEXT("WestPier"))) Target=Field;
        }
        if(!Target){++ArtShot;return;}
        const auto Count=[&]()
        {
            if(auto* Route=Cast<UIslandRouteVoxelComponent>(Target)) return Route->RemainingCount();
            return CastChecked<UIslandMeshVoxels>(Target)->RemainingCount();
        };
        const bool bAfter=(ArtShot%2)==0;
        if(bAfter)
        {
            FVector Cam=ArtCamera->GetActorLocation(), To=ArtCamera->GetActorForwardVector();
            FHitResult Hit;
            auto* Hero=Player->GetHero();
            FCollisionQueryParams Q(SCENE_QUERY_STAT(ArtTool),false,Hero);
            if(Target->LineTraceComponent(Hit,Cam,Cam+To*3500.,Q))
            {
                const FVector Aim=(Hit.ImpactPoint-Cam).GetSafeNormal();
                Hero->SetActorHiddenInGame(true);
                Hero->SetActorLocation(Hit.ImpactPoint-Aim*94.-(Hero->GetAimOrigin()-Hero->GetActorLocation()),false,nullptr,ETeleportType::TeleportPhysics);
                Player->SubmitEdit(EVoxelAction::Punch,Aim);
                UE_LOG(LogTemp,Display,TEXT("ROUTE_PHOTO_SHOVEL target=%s before=%d"),*Target->GetName(),Count());
            }
            ArtCameraFrame=GFrameCounter;
            bArtFramed=true;
            return;
        }
        Focus=Target->Bounds.Origin;
        const FVector Facing=Target->GetOwner()->GetActorForwardVector();
        const FVector Front=FVector::DotProduct(Facing,Sun)<0.?-Facing:Facing;
        const FVector Side=FVector::CrossProduct(Front,FVector::UpVector);
        const FVector Direction=bRock?Sun:(Front+.45*Side).GetSafeNormal2D();
        const double Distance=bRock?465.:1150.;
        Eye=Focus+Direction*Distance+FVector(0,0,bRock?160.:180.);
        Eye.Z=FMath::Max(Eye.Z,Terrain->GetDensity()->SurfaceAt(FVector2D(Eye)).Get(Eye.Z-135.)+135.);
        Fov=bRock?49.f:55.f;
    }
    else if (ArtShot >= 11)
    {
        TArray<AIslandRouteVoxel*> Roadside;
        for (TActorIterator<AIslandRouteVoxel> It(GetWorld()); It; ++It) Roadside.Add(*It);
        if (Roadside.IsEmpty()){ ++ArtShot; return; }
        const bool bRock = ArtShot == 12;
        Roadside.Sort([bRock](const AIslandRouteVoxel& A,const AIslandRouteVoxel& B)
        {
            const auto Kind = bRock ? IslandRouteFlora::EShape::SplitReef : IslandRouteFlora::EShape::FanPalm;
            const bool Left=A.GetKind()==Kind, Right=B.GetKind()==Kind;
            if(Left!=Right) return Left;
            return A.GetActorLocation().SizeSquared2D()<B.GetActorLocation().SizeSquared2D();
        });
        const FVector Subject=Roadside[0]->GetActorLocation();
        const FVector Out=Subject.GetSafeNormal2D();
        auto GroundAt=[&](FVector Point,double Height)
        {
            Point.Z=Terrain->GetDensity()->SurfaceAt(FVector2D(Point)).Get(Subject.Z)+Height;
            return Point;
        };
        if(ArtShot==14)
        {
            Focus=GroundAt(FVector::ZeroVector,40.);
            Eye=Focus+Sun*6500.+FVector(0,0,4850.);
            Fov=62.f;
        }
        else if(ArtShot==13)
        {
            // Frame an actual route from roughly player eye height, with a nearby plant
            // to one side and a distant landmark ahead. Prefer a sightline the terrain permits.
            double Best=-MAX_dbl;
            for(auto* Plant:Roadside)
            {
                if(Plant->GetKind()!=IslandRouteFlora::EShape::FanPalm) continue;
                const FVector P=Plant->GetActorLocation();
                for(TActorIterator<AExpeditionLandmark> It(GetWorld()); It; ++It)
                {
                    FVector Beacon,Extent;
                    It->GetActorBounds(false,Beacon,Extent); Beacon.Z+=Extent.Z*.45;
                    const FVector Along=(Beacon-P).GetSafeNormal2D(), Across=FVector::CrossProduct(Along,FVector::UpVector);
                    for(double Offset:{-380.,380.})
                    {
                        const FVector E=GroundAt(P-Along*1120.+Across*Offset,186.);
                        const FVector F=P+FVector(0,0,113.)+Along*230.+Across*(Offset*.57);
                        const FVector Dir=(F-E).GetSafeNormal();
                        const FVector Right=FVector::CrossProduct(FVector::UpVector,Dir).GetSafeNormal();
                        const FVector Up=FVector::CrossProduct(Dir,Right);
                        const FVector ToMark=Beacon-E;
                        const double Ahead=FVector::DotProduct(ToMark,Dir);
                        if(Ahead<1300.) continue;
                        const double H=FVector::DotProduct(ToMark,Right)/Ahead, V=FVector::DotProduct(ToMark,Up)/Ahead;
                        if(FMath::Abs(H)>.49 || FMath::Abs(V)>.26) continue;
                        double Score=.3+2.*FMath::Abs(H);
                        FHitResult Hit;
                        if(!GetWorld()->LineTraceSingleByChannel(Hit,E,Beacon,ECC_Visibility) || Hit.GetActor()==*It) Score+=7.;
                        for(auto* Other:Roadside)
                        {
                            const FVector D=Other->GetActorLocation()+FVector(0,0,70.)-E;
                            const double Depth=FVector::DotProduct(D,Dir);
                            if(Depth<360. || Depth>4800. || FMath::Abs(FVector::DotProduct(D,Right))>Depth*.49 || FMath::Abs(FVector::DotProduct(D,Up))>Depth*.22) continue;
                            Score+=1.-Depth/6500.;
                        }
                        if(Score>Best){ Best=Score; Eye=E; Focus=F; }
                    }
                }
            }
            if(Best==-MAX_dbl){ Eye=GroundAt(Subject+Sun*1100.,186.); Focus=Subject+FVector(0,0,114.); }
            Fov=64.f;
        }
        else
        {
            Focus=Subject+FVector(0,0,bRock?60.:100.);
            Eye=GroundAt(Subject+Sun*(bRock?650.:840.)+Out*110.,bRock?210.:225.);
            Fov=60.f;
        }
    }
    else
    {
        AActor* Subject = nullptr;
        if (ArtShot < 7)
        {
            for (TActorIterator<AIslandNeutralProp> It(GetWorld()); It; ++It)
                if (int32(It->GetKind()) == ArtShot-2 && (!Subject || It->GetActorLocation().X<Subject->GetActorLocation().X)) Subject = *It;
        }
        else
        {
            for (TActorIterator<AExpeditionLandmark> It(GetWorld()); It; ++It)
                if (It->ClueIndex == ArtShot-7) Subject = *It;
        }
        if (!Subject)
        {
            UE_LOG(LogTemp,Display,TEXT("EXPEDITION_ART_SKIPPED %s absent from this round"),Shots[ArtShot]);
            ++ArtShot;
            return;
        }
        FVector Extent;
        Subject->GetActorBounds(false,Focus,Extent);
        const FVector Across = Subject->GetActorForwardVector();
        FVector Direction = Sun;
        if (ArtShot == 3)
            Direction = (Across * (FVector::DotProduct(Across,Sun)<0.?-1.:1.) - Subject->GetActorRightVector()*.48).GetSafeNormal2D();
        const double Size = FMath::Max3(Extent.Size2D(),Extent.Z*1.65,240.);
        Eye = Focus + Direction*Size*2.2 + FVector(0,0,Size*.68);
        Eye.Z = FMath::Max(Eye.Z, Terrain->GetDensity()->SurfaceAt(FVector2D(Eye)).Get(Eye.Z-140.)+140.);
    }
    TourViewSince=GetWorld()->GetRealTimeSeconds();
    if (!bTour && !bWreckStudy)
        UE_LOG(LogTemp,Display,TEXT("ISLAND_ART_CAMERA shot=%s eye=%s focus=%s fov=%.1f"),
            bInventory ? InventoryShots[ArtShot] : Shots[ArtShot],*Eye.ToString(),*Focus.ToString(),Fov);
    ArtCamera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());
    ArtCamera->GetCameraComponent()->SetFieldOfView(Fov);
    Player->SetViewTarget(ArtCamera);
    ArtCameraFrame = GFrameCounter;
    bArtFramed = true;
}
