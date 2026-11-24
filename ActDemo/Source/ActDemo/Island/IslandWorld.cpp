#include "Island/IslandWorld.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Voxel/VoxelTerrain.h"
#include "Voxel/VoxelCharacter.h"
#include "Island/IslandRules.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AIslandChest::AIslandChest()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.bAllowTickOnDedicatedServer = false;
    USceneComponent* Frame = CreateDefaultSubobject<USceneComponent>(TEXT("Frame"));
    SetRootComponent(Frame);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Prize(TEXT("/Game/ActDemo/Materials/M_Amber"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Clue(TEXT("/Game/ActDemo/Materials/M_LightStone"));
    // Keep exactly one stationary visibility target while the lid moves.
    UStaticMeshComponent* Bounds = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Chest"));
    Bounds->SetupAttachment(Frame);
    Bounds->SetStaticMesh(Cube.Object);
    Bounds->SetRelativeScale3D(IslandRules::ChestExtent / 50.0);
    Bounds->SetVisibility(false);
    Bounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Bounds->SetCollisionResponseToAllChannels(ECR_Ignore);
    Bounds->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    const auto MakePanel = [&](FName Name, USceneComponent* Parent, FVector Center, FVector Extent, UMaterialInterface* Material)
    {
        UStaticMeshComponent* Panel = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Panel->SetupAttachment(Parent);
        Panel->SetStaticMesh(Cube.Object);
        Panel->SetMaterial(0, Material);
        Panel->SetRelativeLocation(Center);
        Panel->SetRelativeScale3D(Extent / 50.0);
        Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Panel->SetCanEverAffectNavigation(false);
        return Panel;
    };
    BoxMesh = MakePanel(TEXT("Body"), Frame, FVector(0, 0, -7.5), FVector(45, 34, 24.5), Clue.Object);
    LidHinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
    LidHinge->SetupAttachment(Frame);
    LidHinge->SetRelativeLocation(FVector(0, -34, 18));
    LidMesh = MakePanel(TEXT("Lid"), LidHinge, FVector(0, 34, 7), FVector(45, 34, 7), Clue.Object);
    ClaspMesh = MakePanel(TEXT("Clasp"), LidHinge, FVector(0, 68, 0), FVector(6, 1.5, 6), Prize.Object);
    PrizeMaterial = Prize.Object;
    ClueMaterial = Clue.Object;
}

void AIslandChest::SetPrize(bool bValue)
{
    bPrize = bValue;
    OnRep_Prize();
}

bool AIslandChest::IsReachableFrom(const AVoxelCharacter& Hero) const
{
    const FVector Origin = Hero.GetAimOrigin();
    const FVector Center = GetActorLocation();
    if (FVector::DistSquared(Origin, Center) > FMath::Square(IslandRules::OpenReach))
    {
        return false;
    }
    FHitResult Hit;
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(IslandReachableChest), false, &Hero);
    return GetWorld()->LineTraceSingleByChannel(Hit, Origin, Center, ECC_Visibility, Query) && Hit.GetActor() == this;
}

void AIslandChest::OnRep_Prize()
{
    BoxMesh->SetMaterial(0, bPrize ? PrizeMaterial : ClueMaterial);
    LidMesh->SetMaterial(0, bPrize ? PrizeMaterial : ClueMaterial);
    ClaspMesh->SetMaterial(0, bPrize ? ClueMaterial : PrizeMaterial);
}

void AIslandChest::AnimateLock(double FinishAt, bool bAssisted)
{
    LocalFinishAt = FinishAt;
    LocalLockDuration = bAssisted ? IslandRules::LockSeconds * .5 : IslandRules::LockSeconds;
    SetActorTickEnabled(true);
}

void AIslandChest::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const AGameStateBase* State = GetWorld()->GetGameState();
    double Target = 0.0;
    if (State && LocalFinishAt > 0)
    {
        const double Remaining = LocalFinishAt - State->GetServerWorldTimeSeconds();
        Target = 65.0 * FMath::Clamp(1.0 - Remaining / LocalLockDuration, 0.0, 1.0);
    }
    LidAngle = FMath::FInterpTo(LidAngle, Target, DeltaSeconds, 14.0);
    if (LocalFinishAt == 0.0 && LidAngle < .1)
    {
        LidAngle = 0.0;
        SetActorTickEnabled(false);
    }
    LidHinge->SetRelativeRotation(FRotator(0, 0, -LidAngle));
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
    const AVoxelTerrain* Terrain = AVoxelTerrain::Find(GetWorld());
    const TArray<FVector2D> Shore = VoxelPlayground::CoastOutline(Terrain ? Terrain->Layout : EVoxelLayout::Island);
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    for (int32 Index = 0; Index < Shore.Num(); ++Index)
    {
        const FVector A(Shore[Index],0.0), B(Shore[(Index + 1) % Shore.Num()],0.0);
        const int32 Base = Vertices.Num();
        Vertices.Append({A, B, B * 18.0, A * 18.0});
        Triangles.Append({Base, Base + 1, Base + 2, Base, Base + 2, Base + 3});
    }
    WaterMesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, {}, {}, {}, {}, false);
    WaterMesh->SetMaterial(0, WaterMaterial);
}
