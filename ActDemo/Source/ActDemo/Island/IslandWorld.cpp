#include "Island/IslandWorld.h"

#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
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
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Coin(TEXT("/Engine/BasicShapes/Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Needle(TEXT("/Engine/BasicShapes/Cone"));
    CoinShape = Coin.Object;
    NeedleShape = Needle.Object;
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

void AIslandChest::MarkChallengeResolved()
{
    if (!HasAuthority() || bPrize || bChallengeResolved) return;
    bChallengeResolved = true;
    OnRep_ChallengeResolved();
    ForceNetUpdate();
}

void AIslandChest::OnRep_ChallengeResolved()
{
    LidHinge->SetRelativeRotation(FRotator(0, 0, -90.));
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

void AIslandChest::RevealPrize(double ServerTime)
{
    if (!HasAuthority() || !bPrize || IsPrizeRevealed()) return;
    PrizeOpenedAt = ServerTime;
    OnRep_PrizeOpened();
    ForceNetUpdate();
}

void AIslandChest::OnRep_PrizeOpened()
{
    if (!IsPrizeRevealed() || Compass || GetNetMode() == NM_DedicatedServer) return;
    Compass = NewObject<USceneComponent>(this);
    Compass->SetupAttachment(GetRootComponent());
    Compass->SetRelativeLocation(FVector(0,0,8));
    Compass->SetRelativeScale3D(FVector(.05));
    Compass->RegisterComponent();
    const auto Piece = [&](UStaticMesh* Shape, UMaterialInterface* Surface, FVector At, FRotator Facing, FVector Size)
    {
        auto* Part = NewObject<UStaticMeshComponent>(this);
        Part->SetupAttachment(Compass);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetCanEverAffectNavigation(false);
        Part->SetStaticMesh(Shape);
        Part->SetMaterial(0, Surface);
        Part->SetRelativeTransform(FTransform(Facing, At, Size/100.));
        Part->RegisterComponent();
    };
    Piece(CoinShape, PrizeMaterial, FVector::ZeroVector, FRotator(90,0,0), FVector(52,52,6));
    for (double Side : {-1.,1.})
    {
        Piece(CoinShape, ClueMaterial, FVector(Side*3.4,0,0), FRotator(90,0,0), FVector(45,45,1));
        for (int32 I=0; I<4; ++I)
        {
            const FRotator NeedleRotation(0,0,I*90.);
            const FVector Along=NeedleRotation.RotateVector(FVector::UpVector);
            Piece(NeedleShape, PrizeMaterial, FVector(Side*5.,0,0)+Along*9., NeedleRotation, FVector(5,11,23));
        }
    }
    auto* Glow = NewObject<UPointLightComponent>(this);
    Glow->SetupAttachment(Compass);
    Glow->SetLightColor(FLinearColor(1.f,.65f,.2f));
    Glow->SetIntensity(55.f);
    Glow->SetAttenuationRadius(170.f);
    Glow->SetCastShadows(false);
    Glow->RegisterComponent();
    SetActorTickEnabled(true);
}

void AIslandChest::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const AGameStateBase* State = GetWorld()->GetGameState();
    double Target = 0.0;
    if (State && IsPrizeRevealed())
    {
        const double Age = FMath::Max(0.,State->GetServerWorldTimeSeconds()-PrizeOpenedAt);
        const double Rise = FMath::SmoothStep(0.,1.05,Age);
        Target = 105.;
        if (Compass)
        {
            Compass->SetRelativeLocation(FVector(0,0,FMath::Lerp(8.,98.,Rise)));
            Compass->SetRelativeRotation(FRotator(0, FMath::Lerp(-110.,28.,Rise), 0));
            Compass->SetRelativeScale3D(FVector(.05+.95*Rise));
        }
        if (Age>=1.2 && FMath::Abs(LidAngle-Target)<.1) SetActorTickEnabled(false);
    }
    else if (State && LocalFinishAt > 0)
    {
        const double Remaining = LocalFinishAt - State->GetServerWorldTimeSeconds();
        Target = 65.0 * FMath::Clamp(1.0 - Remaining / LocalLockDuration, 0.0, 1.0);
    }
    LidAngle = FMath::FInterpTo(LidAngle, Target, DeltaSeconds, 14.0);
    if (!IsPrizeRevealed() && LocalFinishAt == 0.0 && LidAngle < .1)
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
    DOREPLIFETIME(AIslandChest, bChallengeResolved);
    DOREPLIFETIME(AIslandChest, PrizeOpenedAt);
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
