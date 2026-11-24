#include "ExpeditionMapBuilder.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Island/ExpeditionDock.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandWorld.h"
#include "Misc/PackageName.h"
#include "Voxel/VoxelTerrain.h"
#include "EngineUtils.h"

namespace
{
    constexpr auto Layout = EVoxelLayout::Expedition;
    int32 Ground(double X, double Y) { return VoxelPlayground::BaseHeight(Layout,FMath::RoundToInt(X),FMath::RoundToInt(Y)); }

    struct FWhitebox
    {
        UWorld& World;
        UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
        UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
        UMaterialInterface* Pale = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/ActDemo/Materials/M_LightStone.M_LightStone"));
        UMaterialInterface* Slate = LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));

        // Local shape dimensions in cm. All landmark components are editable in the saved level.
        void Piece(AActor& Owner, const TCHAR* Name, FVector Offset, FVector Size, bool bRound=false, bool bPale=true, FRotator R=FRotator::ZeroRotator)
        {
            auto* Mesh=NewObject<UStaticMeshComponent>(&Owner,MakeUniqueObjectName(&Owner,UStaticMeshComponent::StaticClass(),Name),RF_Transactional);
            Mesh->SetupAttachment(Owner.GetRootComponent());
            Mesh->SetStaticMesh(bRound?Cylinder:Cube);
            Mesh->SetMaterial(0,bPale?Pale:Slate);
            Mesh->SetRelativeTransform(FTransform(R,Offset,Size/100.0));
            Owner.AddInstanceComponent(Mesh);
            Mesh->RegisterComponent();
        }
    };

    void Landmark(FWhitebox& Art, int32 Index, const FVector2D& Zone)
    {
        const TCHAR* Names[]={TEXT("A • Broken arch"),TEXT("B • Signal needles"),TEXT("C • Quarry steps"),TEXT("D • Leaning fin")};
        const FVector2D Out = Zone.GetSafeNormal();
        const FVector2D P=Zone+Out*950.;
        auto* Mark=Art.World.SpawnActor<AExpeditionLandmark>(FVector(P,Ground(P.X,P.Y)),FVector(Out,0).Rotation());
        Mark->ClueIndex=Index;
        Mark->SetActorLabel(Names[Index]);
        Mark->SetFolderPath(TEXT("Excavation landmarks"));
        for (int32 Face = 0; Face < 2; ++Face)
        {
            auto* Letter = NewObject<UTextRenderComponent>(Mark,NAME_None,RF_Transactional);
            Letter->SetupAttachment(Mark->GetRootComponent());
            Letter->SetRelativeLocation({Face==0?320.:-320., 0., 230.});
            Letter->SetRelativeRotation(FRotator(0,Face==0?0:180,0));
            Letter->SetText(FText::FromString(FString::Printf(TEXT("%c"),'A'+Index)));
            Letter->SetHorizontalAlignment(EHTA_Center);
            Letter->SetWorldSize(124.f);
            Letter->SetTextRenderColor(FColor(68,84,91));
            Letter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Mark->AddInstanceComponent(Letter);
            Letter->RegisterComponent();
        }
        if (Index==0)
        {
            Art.Piece(*Mark,TEXT("WestPier"),{0,-260,360},{260,210,720});
            Art.Piece(*Mark,TEXT("EastPier"),{0,260,315},{260,210,630});
            Art.Piece(*Mark,TEXT("Lintel"),{0,-90,715},{255,575,145},false,true,{0,0,-5});
        }
        else if (Index==1)
        {
            Art.Piece(*Mark,TEXT("TallNeedle"),{0,-160,560},{190,190,1120},true);
            Art.Piece(*Mark,TEXT("ShortNeedle"),{0,150,395},{180,180,790},true,false);
            Art.Piece(*Mark,TEXT("Crown"),{0,-160,1090},{290,290,70},true,false);
        }
        else if (Index==2)
        {
            for (int32 Step=0; Step<5; ++Step)
            {
                const double H=(Step+1)*112.;
                Art.Piece(*Mark,TEXT("Bench"),{Step*155.-310,0,H/2},{165,800.-Step*84,H},false,Step%2==0);
            }
        }
        else
        {
            Art.Piece(*Mark,TEXT("Blade"),{0,0,475},{225,440,1020},false,true,{0,0,-23});
            Art.Piece(*Mark,TEXT("Heel"),{140,250,120},{490,280,240},false,false);
        }
    }

    bool CheckLandingRoutes(UWorld& World, AVoxelTerrain& Terrain)
    {
        double Shortest = TNumericLimits<double>::Max(), Longest = 0.;
        TArray<UStaticMeshComponent*> Obstacles;
        for (TActorIterator<AActor> It(&World); It; ++It)
        {
            TInlineComponentArray<UStaticMeshComponent*> Geometry(*It);
            for (auto* Component : Geometry)
                if (Component->GetStaticMesh() && Component->GetCollisionResponseToChannel(ECC_Visibility)==ECR_Block)
                    Obstacles.Add(Component);
        }
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(ExpeditionRoute),true);
        const auto TraceFloor = [&](FVector2D XY, FHitResult& Floor)
        {
            const FVector From(XY,2000.), To(XY,-300.);
            bool bFound = Terrain.TraceEditorPreview(From,To,Floor);
            for (UStaticMeshComponent* Mesh : Obstacles)
            {
                if (!FMath::LineBoxIntersection(Mesh->Bounds.GetBox(),From,To,To-From)) continue;
                FHitResult Candidate;
                if (Mesh->LineTraceComponent(Candidate,From,To,Query) && (!bFound || Candidate.Time<Floor.Time))
                {
                    Floor=Candidate;
                    bFound=true;
                }
            }
            return bFound;
        };
        for (TActorIterator<AExpeditionDock> Dock(&World); Dock; ++Dock)
        {
            const FVector Start = Dock->GetActorLocation();
            const FVector Inland = Dock->GetActorForwardVector();
            const FVector Shore = Start + Inland * 670.;
            double Nearest = TNumericLimits<double>::Max();
            FVector2D Target = FVector2D::ZeroVector;
            for (const FIslandSearchCircle& Area : Terrain.GetSearchCircles())
            {
                const FVector2D Center(Area.Center);
                const double Distance = FVector2D::Distance(FVector2D(Shore), Center);
                if (Distance >= Nearest) continue;
                Nearest = Distance;
                Target = Center + (FVector2D(Shore) - Center).GetSafeNormal() * Area.Radius;
            }

            double Distance = 0.;
            TOptional<FVector> Previous;
            const auto Walk = [&](const FVector2D& From, const FVector2D& To)
            {
                const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(From,To)/55.));
                for (int32 Step = 0; Step <= Steps; ++Step)
                {
                    const FVector2D XY = FMath::Lerp(From,To,double(Step)/Steps);
                    FHitResult Hit;
                    bool bFloor = TraceFloor(XY,Hit);
                    // In a commandlet, StaticMeshComponents may not have live Chaos bodies.
                    // Use the authored cube's transformed deck top while the route is on its footprint.
                    const auto& Mesh = Dock->GetMesh();
                    const FVector Local = Mesh.GetComponentTransform().InverseTransformPosition(FVector(XY, Mesh.GetComponentLocation().Z));
                    const FBox DeckBounds = Mesh.GetStaticMesh()->GetBoundingBox();
                    const FVector DeckTop = Mesh.GetComponentTransform().TransformPosition({Local.X,Local.Y,DeckBounds.Max.Z});
                    if (Local.X>=DeckBounds.Min.X && Local.X<=DeckBounds.Max.X && Local.Y>=DeckBounds.Min.Y && Local.Y<=DeckBounds.Max.Y
                        && (!bFloor || DeckTop.Z>Hit.ImpactPoint.Z))
                    {
                        bFloor=true;
                        Hit.ImpactPoint=DeckTop;
                        Hit.ImpactNormal=Mesh.GetUpVector();
                    }
                    if (!bFloor || Hit.ImpactNormal.Z < .70 || Hit.ImpactPoint.Z < -25.)
                    {
                        UE_LOG(LogTemp,Error,TEXT("EXPEDITION_ROUTE_FLOOR dock=%d xy=%s z=%.1f normal=%.2f actor=%s"),Dock->DockIndex,*XY.ToString(),Hit.ImpactPoint.Z,Hit.ImpactNormal.Z,*GetNameSafe(Hit.GetActor()));
                        return false;
                    }
                    if (Previous.IsSet())
                    {
                        if (FMath::Abs(Hit.ImpactPoint.Z-Previous->Z)>48.)
                        {
                            UE_LOG(LogTemp,Error,TEXT("EXPEDITION_ROUTE_STEP dock=%d xy=%s prev=%.1f z=%.1f actor=%s"),Dock->DockIndex,*XY.ToString(),Previous->Z,Hit.ImpactPoint.Z,*GetNameSafe(Hit.GetActor()));
                            return false;
                        }
                        Distance += FVector::Distance(Hit.ImpactPoint,Previous.GetValue());
                    }
                    Previous = Hit.ImpactPoint;
                }
                return true;
            };
            if (!Walk(FVector2D(Start),FVector2D(Shore)) || !Walk(FVector2D(Shore),Target))
            {
                UE_LOG(LogTemp,Error,TEXT("EXPEDITION_ROUTE_BLOCKED dock=%d"),Dock->DockIndex);
                return false;
            }
            const double NormalizedSeconds = Distance/370.;
            Shortest = FMath::Min(Shortest,NormalizedSeconds);
            Longest = FMath::Max(Longest,NormalizedSeconds);
        }
        UE_LOG(LogTemp,Display,TEXT("EXPEDITION_ROUTES docks=8 speed=370 quickest=%.2fs slowest=%.2fs spread=%.2fs"),Shortest,Longest,Longest-Shortest);
        return Longest - Shortest <= 1.;
    }

    bool Verify(UWorld& World)
    {
        AVoxelTerrain* Terrain=AVoxelTerrain::Find(&World);
        if (!Terrain || Terrain->Layout!=Layout) return false;
        TSet<int32> Docks,Marks;
        for (TActorIterator<AExpeditionDock> It(&World); It; ++It)
        {
            if (It->Berths.Num()!=8 || Docks.Contains(It->DockIndex)) return false;
            Docks.Add(It->DockIndex);
        }
        for (TActorIterator<AExpeditionLandmark> It(&World); It; ++It) Marks.Add(It->ClueIndex);
        if (Docks.Num()!=8 || Marks.Num()!=4) return false;
        Terrain->RebuildEditorPreview();
        const FVoxelDensity* Density=Terrain->GetEditorDensity();
        FIslandTreasureLayout Hunt;
        FString Error;
        bool bValid=CheckLandingRoutes(World,*Terrain);
        bValid &= Density && IslandRules::GenerateLayout(0,0,Terrain->GetSearchCircles(),*Density,Hunt,&Error,true);
        FHitResult Hit;
        bValid &= Terrain->TraceEditorPreview({0,0,1600},{0,0,-600},Hit);
        const int32 Height=Ground(0,0);
        const FVoxelBrushStamp Stroke{{0,0,Height},125,false};
        const double Before=Hit.ImpactPoint.Z;
        // The verification world is transient to the process. Never save the test dig.
        bValid &= Terrain->ApplyEditorStamps(MakeArrayView(&Stroke,1));
        bValid &= Terrain->TraceEditorPreview({0,0,1600},{0,0,-600},Hit) && Hit.ImpactPoint.Z < Before-70;
        Terrain->ClearEditorPreview();
        UE_LOG(LogTemp,Display,TEXT("EXPEDITION_MAP_CHECK docks=%d landmarks=%d valid=%d error=%s"),Docks.Num(),Marks.Num(),bValid,*Error);
        return bValid;
    }
}

int32 BuildExpeditionMap(const FString& Params)
{
    constexpr const TCHAR* Path=TEXT("/Game/TreasureIsland/L_ExpeditionIsland");
    if (FPackageName::DoesPackageExist(Path))
    {
        const bool bRebake = FParse::Param(*Params,TEXT("RebakeExpeditionSurface"));
        const bool bVerify = FParse::Param(*Params,TEXT("VerifyExpedition"));
        if (bRebake || bVerify)
        {
            UWorld* Existing=UEditorLoadingAndSavingUtils::LoadMap(Path);
            if (!Existing) return 1;
            if (bRebake)
            {
                AVoxelTerrain* Terrain=AVoxelTerrain::Find(Existing);
                if (!Terrain || Terrain->Layout!=Layout) return 1;
                Terrain->BakeExpeditionSurface();
                for (TActorIterator<AExpeditionDock> Dock(Existing); Dock; ++Dock) Dock->RefreshLandingNumber();
                if (!UEditorLoadingAndSavingUtils::SaveMap(Existing,Path)) return 1;
                UE_LOG(LogTemp,Display,TEXT("EXPEDITION_SURFACE_BAKED preserved authored actors and brush strokes"));
            }
            if (bVerify && !Verify(*Existing)) return 1;
        }
        UE_LOG(LogTemp, Display, TEXT("VOXEL_MAP_READY %s (existing authored map)"),Path);
        return 0;
    }

    UWorld* World=UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!World) return 1;
    FWhitebox Art{*World};
    World->GetWorldSettings()->DefaultGameMode=AIslandGameMode::StaticClass();
    auto* Terrain=World->SpawnActor<AVoxelTerrain>();
    Terrain->SetActorLabel(TEXT("Excavatable rock • edit with Voxel Map"));
    Terrain->Layout=Layout;
    auto* Sea=World->SpawnActor<AIslandWater>();
    Sea->WaterMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/TreasureIsland/M_Sea.M_Sea"));
    const FIntPoint Regions[]={{4300,1780},{-1780,4300},{-4300,-1780},{1780,-4300}};
    for (int32 Index=0;Index<4;++Index)
    {
        Terrain->SetSearchCircle(Index,{Regions[Index],410});
        Landmark(Art,Index,FVector2D(Regions[Index]));
    }
    Terrain->BakeExpeditionSurface();
    for (int32 Index=0;Index<8;++Index)
    {
        const double Angle=Index*UE_PI/4.;
        const FVector Out(FMath::Cos(Angle),FMath::Sin(Angle),0);
        const FVector Center=Out*8010.+FVector(0,0,37);
        const FRotator Facing=(-Out).Rotation();
        auto* Dock=World->SpawnActor<AExpeditionDock>(Center,Facing);
        Dock->SetActorLabel(FString::Printf(TEXT("Landing %02d"),Index+1));
        Dock->SetFolderPath(TEXT("Crew landings"));
        Dock->DockIndex=Index;
        Dock->GetMesh().SetStaticMesh(Art.Cube);
        Dock->GetMesh().SetMaterial(0,Art.Slate);
        Dock->GetMesh().SetRelativeScale3D({10.2,7.8,.48});
        Dock->RefreshLandingNumber();
        if (Index==0) World->SpawnActor<APlayerStart>(Center+FVector(0,0,126),Facing);
    }

    // Paired low cairns mark the coast trail, while single tall cairns point toward the interior.
    for (int32 Index=0;Index<16;++Index)
    {
        const double Angle=(Index+.5)*UE_TWO_PI/16.;
        const FVector Radial(FMath::Cos(Angle),FMath::Sin(Angle),0);
        const FVector Tangent(-Radial.Y,Radial.X,0);
        for (int32 Marker=0;Marker<3;++Marker)
        {
            const FVector P=Radial*(Marker==2 ? 2900.:6060.)+Tangent*(Marker==1?105.:0.);
            const double H=Marker==2?210.:82.;
            auto* Post=World->SpawnActor<AStaticMeshActor>(FVector(P.X,P.Y,Ground(P.X,P.Y)+H*.49), Radial.Rotation());
            Post->SetActorLabel(Marker==2?TEXT("Inner trail beacon"):TEXT("Coast trail cairn"));
            Post->SetFolderPath(TEXT("Trail markers"));
            auto* Mesh=Post->GetStaticMeshComponent();
            Mesh->SetStaticMesh(Marker==2?Art.Cylinder:Art.Cube);
            Mesh->SetMaterial(0,Marker==2?Art.Pale:Art.Slate);
            Mesh->SetWorldScale3D({.55,.55,H/100.});
        }
    }
    auto* Sun=World->SpawnActor<ADirectionalLight>(FVector(0,0,1200),FRotator(-48,-34,0));
    Sun->SetMobility(EComponentMobility::Movable);
    Sun->GetLightComponent()->SetIntensity(5.f);
    CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent())->bAtmosphereSunLight=true;
    World->SpawnActor<ASkyAtmosphere>();
    auto* Sky=World->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->SetIntensity(1.5f);
    auto* Lighting=World->SpawnActor<APostProcessVolume>();
    Lighting->bUnbound=true;
    Lighting->Settings.bOverride_DynamicGlobalIlluminationMethod=true;
    Lighting->Settings.DynamicGlobalIlluminationMethod=EDynamicGlobalIlluminationMethod::None;
    Lighting->Settings.bOverride_ReflectionMethod=true;
    Lighting->Settings.ReflectionMethod=EReflectionMethod::None;
    if (!UEditorLoadingAndSavingUtils::SaveMap(World,Path)) return 1;
    UE_LOG(LogTemp,Display,TEXT("VOXEL_MAP_READY %s (eight landings, four excavation shelves, baked surface)"),Path);
    return FParse::Param(*Params,TEXT("VerifyExpedition")) && !Verify(*World) ? 1 : 0;
}
