#include "ActVoxelMapCommandlet.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/PostProcessVolume.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandRules.h"
#include "Island/IslandWorld.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "UObject/SavePackage.h"
#include "Voxel/VoxelGameMode.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    UMaterial* IslandSeaMaterial()
    {
        const TCHAR* AssetPath = TEXT("/Game/TreasureIsland/M_Sea");
        if (FPackageName::DoesPackageExist(AssetPath))
        {
            return LoadObject<UMaterial>(nullptr, TEXT("/Game/TreasureIsland/M_Sea.M_Sea"));
        }

        UPackage* Package = CreatePackage(AssetPath);
        UMaterial* Material = NewObject<UMaterial>(Package, TEXT("M_Sea"), RF_Public | RF_Standalone);
        Material->SetShadingModel(MSM_Unlit);
        Material->TwoSided = true;
        auto* Color = NewObject<UMaterialExpressionConstant3Vector>(Material);
        Color->Constant = FLinearColor(.038f, .18f, .23f);
        Material->GetExpressionCollection().AddExpression(Color);
        Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Color);
        Material->PostEditChange();
        FAssetRegistryModule::AssetCreated(Material);
        const FString Filename = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Package, Material, *Filename, Args) ? Material : nullptr;
    }

    bool BuildIsland(UWorld& World, const TCHAR* MapPath)
    {
        UMaterial* Sea = IslandSeaMaterial();
        if (!Sea)
        {
            return false;
        }
        World.GetWorldSettings()->DefaultGameMode = AIslandGameMode::StaticClass();
        AVoxelTerrain* Terrain = World.SpawnActor<AVoxelTerrain>();
        Terrain->SetActorLabel(TEXT("Editable Island"));
        Terrain->Layout = EVoxelLayout::Island;
        World.SpawnActor<AIslandWater>()->WaterMaterial = Sea;

        UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube"));
        auto* DeckMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray"));
        const FVector DockCenters[] = {{-2430, 0, 53}, {2430, 0, 53}, {0, 2430, 53}};
        const double InwardYaw[] = {0, 180, -90};
        for (int32 Team = 0; Team < IslandRules::TeamCount; ++Team)
        {
            const FRotator Facing(0, InwardYaw[Team], 0);
            AStaticMeshActor* Dock = World.SpawnActor<AStaticMeshActor>(DockCenters[Team], Facing);
            Dock->SetActorLabel(FString::Printf(TEXT("%s Dock"), IslandRules::TeamName(Team)));
            Dock->GetStaticMeshComponent()->SetStaticMesh(Cube);
            Dock->GetStaticMeshComponent()->SetMaterial(0, DeckMaterial);
            Dock->SetActorScale3D(FVector(7.2, 3.9, .48));
            for (int32 Member = 0; Member < 2; ++Member)
            {
                const FVector Start = DockCenters[Team] + Facing.RotateVector(FVector(-74, Member == 0 ? -87 : 87, 121));
                World.SpawnActor<APlayerStart>(Start, Facing)->PlayerStartTag =
                    FName(*FString::Printf(TEXT("Seat%d"), Team + Member * IslandRules::TeamCount));
            }
            ATextRenderActor* Label = World.SpawnActor<ATextRenderActor>(DockCenters[Team] + FVector(0, 0, 284), Facing + FRotator(0, 180, 0));
            Label->GetTextRender()->SetText(FText::FromString(IslandRules::TeamName(Team)));
            Label->GetTextRender()->SetWorldSize(42.f);
            Label->GetTextRender()->SetHorizontalAlignment(EHTA_Center);
            Label->GetTextRender()->SetTextRenderColor(IslandRules::TeamColor(Team).ToFColor(true));
        }
        ADirectionalLight* Sun = World.SpawnActor<ADirectionalLight>(FVector(0, 0, 700), FRotator(-54, -35, 0));
        Sun->SetMobility(EComponentMobility::Movable);
        Sun->GetLightComponent()->SetIntensity(4.5f);
        CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent())->bAtmosphereSunLight = true;
        World.SpawnActor<ASkyAtmosphere>();
        ASkyLight* Sky = World.SpawnActor<ASkyLight>();
        Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
        Sky->GetLightComponent()->SetIntensity(1.2f);
        APostProcessVolume* Whitebox = World.SpawnActor<APostProcessVolume>();
        Whitebox->bUnbound = true;
        Whitebox->Settings.bOverride_DynamicGlobalIlluminationMethod = true;
        Whitebox->Settings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
        Whitebox->Settings.bOverride_ReflectionMethod = true;
        Whitebox->Settings.ReflectionMethod = EReflectionMethod::None;
        return UEditorLoadingAndSavingUtils::SaveMap(&World, MapPath);
    }
}

UActVoxelMapCommandlet::UActVoxelMapCommandlet()
{
    IsClient = false;
    IsEditor = true;
    LogToConsole = true;
}

int32 UActVoxelMapCommandlet::Main(const FString& Params)
{
    const bool bIsland = FParse::Param(*Params, TEXT("Island"));
    const TCHAR* MapPath = bIsland ? TEXT("/Game/TreasureIsland/L_TreasureIsland") : TEXT("/Game/VoxelPlayground/L_VoxelPlayground");
    if (FPackageName::DoesPackageExist(MapPath) && !FParse::Param(*Params, TEXT("Force")))
    {
        UE_LOG(LogTemp, Display, TEXT("VOXEL_MAP_READY %s (existing)"), MapPath);
        return 0;
    }

    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!World)
    {
        return 1;
    }
    if (bIsland)
    {
        if (!BuildIsland(*World, MapPath))
        {
            return 1;
        }
        UE_LOG(LogTemp, Display, TEXT("VOXEL_MAP_READY %s"), MapPath);
        return 0;
    }
    World->GetWorldSettings()->DefaultGameMode = AVoxelGameMode::StaticClass();
    World->SpawnActor<AVoxelTerrain>()->SetActorLabel(TEXT("Editable Rock"));

    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube"));
    UMaterialInterface* DeckMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray"));
    AStaticMeshActor* Deck = World->SpawnActor<AStaticMeshActor>(FVector(-168, 0, -26), FRotator::ZeroRotator);
    Deck->SetActorLabel(TEXT("Fixed Start Platform"));
    Deck->GetStaticMeshComponent()->SetStaticMesh(Cube);
    if (DeckMaterial)
    {
        Deck->GetStaticMeshComponent()->SetMaterial(0, DeckMaterial);
    }
    Deck->SetActorScale3D(FVector(3.4, 12.0, .52));

    World->SpawnActor<APlayerStart>(FVector(-205, -175, 100), FRotator(0, 0, 0))->PlayerStartTag = TEXT("Host");
    World->SpawnActor<APlayerStart>(FVector(-205, -15, 100), FRotator(0, 0, 0))->PlayerStartTag = TEXT("Guest");

    ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(FVector(0, 0, 700), FRotator(-45, -35, 0));
    Sun->SetMobility(EComponentMobility::Movable);
    Sun->GetLightComponent()->SetIntensity(5.f);
    CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent())->bAtmosphereSunLight = true;
    World->SpawnActor<ASkyAtmosphere>();
    ASkyLight* Sky = World->SpawnActor<ASkyLight>();
    Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Sky->GetLightComponent()->SetIntensity(0.85f);
    for (const FVector Position : {FVector(-180, -120, 310), FVector(700, -350, 380), FVector(800, 315, 425)})
    {
        APointLight* Fill = World->SpawnActor<APointLight>(Position, FRotator::ZeroRotator);
        Fill->GetLightComponent()->SetMobility(EComponentMobility::Movable);
        Fill->GetLightComponent()->SetIntensity(2200.f);
        CastChecked<UPointLightComponent>(Fill->GetLightComponent())->SetAttenuationRadius(750.f);
    }

    const auto Sign = [&](const FVector& Location, const TCHAR* Label)
    {
        ATextRenderActor* SignActor = World->SpawnActor<ATextRenderActor>(Location, FRotator(0, 180, 0));
        UTextRenderComponent* Text = SignActor->GetTextRender();
        Text->SetText(FText::FromString(Label));
        Text->SetWorldSize(30.f);
        Text->SetHorizontalAlignment(EHTA_Center);
        Text->SetTextRenderColor(FColor(232, 236, 243));
    };
    Sign({355, -210, 320}, TEXT("WALL"));
    Sign({710, -300, 142}, TEXT("SHAFT"));
    Sign({935, 360, 350}, TEXT("CLIMB / FILL"));

    if (!UEditorLoadingAndSavingUtils::SaveMap(World, MapPath))
    {
        return 1;
    }
    UE_LOG(LogTemp, Display, TEXT("VOXEL_MAP_READY %s"), MapPath);
    return 0;
}
