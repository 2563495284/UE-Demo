#include "Island/IslandWorld.h"

#include "Components/StaticMeshComponent.h"
#include "Island/IslandRules.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AIslandChest::AIslandChest()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    BoxMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Chest"));
    SetRootComponent(BoxMesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Prize(TEXT("/Game/ActDemo/Materials/M_Amber"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Clue(TEXT("/Game/ActDemo/Materials/M_LightStone"));
    BoxMesh->SetStaticMesh(Cube.Object);
    BoxMesh->SetMaterial(0, Clue.Object);
    BoxMesh->SetRelativeScale3D(IslandRules::ChestExtent / 50.0);
    BoxMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    BoxMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    BoxMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    PrizeMaterial = Prize.Object;
}

void AIslandChest::SetPrize(bool bValue)
{
    bPrize = bValue;
    OnRep_Prize();
}

void AIslandChest::OnRep_Prize()
{
    if (bPrize && PrizeMaterial)
    {
        BoxMesh->SetMaterial(0, PrizeMaterial);
    }
}

void AIslandChest::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AIslandChest, bPrize);
}

AIslandWater::AIslandWater()
{
    WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Sea"));
    SetRootComponent(WaterMesh);
    WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WaterMesh->SetCastShadow(false);
    WaterMesh->SetCanEverAffectNavigation(false);
}

void AIslandWater::BeginPlay()
{
    Super::BeginPlay();
    // The opening follows the same octagonal shore used by the authoritative water test.
    const FVector Shore[] = {{2300, 950, 0}, {950, 2300, 0}, {-950, 2300, 0}, {-2300, 950, 0},
                             {-2300, -950, 0}, {-950, -2300, 0}, {950, -2300, 0}, {2300, -950, 0}};
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Shore); ++Index)
    {
        const FVector A = Shore[Index], B = Shore[(Index + 1) % UE_ARRAY_COUNT(Shore)];
        const int32 Base = Vertices.Num();
        Vertices.Append({A, B, B * 18.0, A * 18.0});
        Triangles.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
    }
    WaterMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, {}, {}, {}, {}, false);
    WaterMesh->SetMaterial(0, WaterMaterial);
}
