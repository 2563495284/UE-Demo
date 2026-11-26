#include "ActIslandFrontCommandlet.h"
#include "ExpeditionDaylight.h"
#include "ExpeditionSceneryArt.h"
#include "Island/IslandFrontMode.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "FileHelpers.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/PackageName.h"

UActIslandFrontCommandlet::UActIslandFrontCommandlet()
{
    IsClient = IsServer = false;
}

int32 UActIslandFrontCommandlet::Main(const FString& Params)
{
    const TCHAR* Path = TEXT("/Game/TreasureIsland/L_IslandFront");
    if (FPackageName::DoesPackageExist(Path))
    {
        const bool bScenery = FParse::Param(*Params,TEXT("RefreshScenery"));
        const bool bLighting = FParse::Param(*Params,TEXT("RefreshLighting"));
        if (!bScenery && !bLighting) return 0;
        UWorld* Existing = UEditorLoadingAndSavingUtils::LoadMap(Path);
        if (!Existing || (bScenery && !ExpeditionSceneryArt::DressHarbour(*Existing))) return 1;
        if (bLighting) ExpeditionDaylight::Apply(*Existing, ExpeditionDaylight::ELocation::Harbour);
        return UEditorLoadingAndSavingUtils::SaveMap(Existing,Path) ? 0 : 1;
    }
    UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(false);
    if (!World) return 1;
    World->GetWorldSettings()->DefaultGameMode = AIslandFrontMode::StaticClass();
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube"));
    auto* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder"));
    auto* Deck = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray"));
    auto* Water = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/TreasureIsland/M_Sea"));
    auto Shape = [&](const TCHAR* Label, FVector Center, FVector Size, UStaticMesh* Mesh, UMaterialInterface* Surface, FRotator Rotation = FRotator::ZeroRotator)
    {
        auto* Actor = World->SpawnActor<AStaticMeshActor>(Center, Rotation);
        Actor->SetActorLabel(Label);
        auto* Component = Actor->GetStaticMeshComponent();
        Component->SetStaticMesh(Mesh);
        if (Surface) Component->SetMaterial(0, Surface);
        Actor->SetActorScale3D(Size / 100.);
    };
    Shape(TEXT("Harbour water"), {0,0,-80}, {300000,300000,100}, Cube, Water);
    // The loading jetty continues beyond the bottom edge of the frame. Mooring posts frame its far end.
    for (int32 I = 0; I < 27; ++I)
        Shape(TEXT("Jetty | board"), {-900.+I*55.,45,5}, {53,335,32}, Cube, Deck);
    for (int32 I = 0; I < 5; ++I)
        for (int32 Side : {-1,1})
            Shape(TEXT("Jetty | mooring pile"), {-760.+I*300.,45.+Side*183.,5}, {35,35,195}, Cylinder, Deck);
    Shape(TEXT("Dock crane | mast"), {226,253,228}, {42,42,475}, Cube, Deck);
    Shape(TEXT("Dock crane | arm"), {218,363,447}, {37,265,34}, Cube, Deck, {0,0,-8});
    Shape(TEXT("Dock crane | hoist"), {218,469,344}, {7,7,165}, Cylinder, Deck);
    Shape(TEXT("Dock crane | cargo"), {218,469,234}, {105,100,68}, Cube, Deck);
    Shape(TEXT("Stacked cargo"), {97,139,61}, {102,102,89}, Cube, Deck, {0,8,0});
    Shape(TEXT("Stacked cargo"), {115,150,130}, {73,76,48}, Cube, Deck, {0,-6,0});
    // A succession of silhouettes gives the player a direction to depart in.
    for (int32 I=0; I<9; ++I)
    {
        const FVector P(4100.+(I%3)*440, -2600.+I*640, -210.+(I%4)*90);
        Shape(TEXT("Far island | stratum"), P, {1650,1080,720.+(I%3)*120}, Cube, Deck,
            FRotator(8.+I%3*7., (I*27)%90, -5.+(I%3)*7.));
    }
    Shape(TEXT("Far island | arch west"), {4800,450,710}, {175,180,980}, Cube, Deck, {0,12,-4});
    Shape(TEXT("Far island | arch east"), {4800,910,707}, {180,160,960}, Cube, Deck, {0,15,8});
    Shape(TEXT("Far island | arch lintel"), {4800,678,1140}, {250,740,180}, Cube, Deck, {0,12,7});
    auto* Cam = World->SpawnActor<ACameraActor>(FVector(-740,-588,350), FRotator(-7.5,26,0));
    Cam->Tags.Add(TEXT("HarbourView"));
    Cam->GetCameraComponent()->SetFieldOfView(62.f);
    Cam->GetCameraComponent()->bConstrainAspectRatio = false;
    Cam->SetActorLabel(TEXT("Harbour entrance view"));
    ExpeditionDaylight::Apply(*World, ExpeditionDaylight::ELocation::Harbour);
    return ExpeditionSceneryArt::DressHarbour(*World) && UEditorLoadingAndSavingUtils::SaveMap(World, Path) ? 0 : 1;
}
