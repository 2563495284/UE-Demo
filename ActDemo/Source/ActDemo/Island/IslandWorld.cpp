#include "Island/IslandWorld.h"

#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Voxel/VoxelTerrain.h"
#include "Voxel/VoxelCharacter.h"
#include "Island/IslandRules.h"
#include "Island/IslandSceneryVoxels.h"
#include "Island/IslandScenerySupport.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AIslandChest::AIslandChest()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.bAllowTickOnDedicatedServer = true;
    USceneComponent* Frame = CreateDefaultSubobject<USceneComponent>(TEXT("Frame"));
    SetRootComponent(Frame);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Coin(TEXT("/Game/TreasureIsland/Art/Crew/SM_CofferRim"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Block(TEXT("/Game/TreasureIsland/Art/Neutral/SM_HewnBlock"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Coffer(TEXT("/Game/TreasureIsland/Art/Crew/SM_CofferBody"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Crown(TEXT("/Game/TreasureIsland/Art/Crew/SM_CofferLid"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Needle(TEXT("/Game/TreasureIsland/Art/Crew/SM_CofferHand"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Dial(TEXT("/Game/TreasureIsland/Art/Crew/SM_CofferDial"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Hoop(TEXT("/Game/TreasureIsland/Art/Crew/SM_CofferHoop"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Keyway(TEXT("/Game/TreasureIsland/Art/Crew/SM_CofferKeyway"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Hasp(TEXT("/Game/TreasureIsland/Art/Crew/SM_CofferHasp"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Gilding(TEXT("/Game/TreasureIsland/Art/Crew/MI_CofferGilding"));
    DialShape = Dial.Object;
    GildedLacquer = Gilding.Object;
    CoinShape = Coin.Object;
    NeedleShape = Needle.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Prize(TEXT("/Game/TreasureIsland/Art/Crew/MI_CofferBrass"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Iron(TEXT("/Game/TreasureIsland/Art/Crew/MI_CofferBronze"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Face(TEXT("/Game/TreasureIsland/Art/Crew/MI_CofferEnamel"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Clue(TEXT("/Game/TreasureIsland/Art/Crew/MI_CofferLacquer"));
    const auto MakePanel = [&](FName Name, USceneComponent* Parent, FVector Center, FVector Extent, UMaterialInterface* Material, UStaticMesh* Shape)
    {
        UStaticMeshComponent* Panel = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Panel->SetupAttachment(Parent);
        Panel->SetStaticMesh(Shape);
        Panel->SetReceivesDecals(false);
        Panel->SetMaterial(0, Material);
        Panel->SetRelativeLocation(Center);
        Panel->SetRelativeScale3D(Extent / 50.0);
        Panel->SetCollisionResponseToAllChannels(ECR_Ignore);
        Panel->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
        Panel->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Panel->SetCanEverAffectNavigation(false);
        return Panel;
    };
    BoxMesh = MakePanel(TEXT("Body"), Frame, FVector(0, 0, -7.5), FVector(45, 34, 24.5), Clue.Object, Coffer.Object);
    LidHinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
    LidHinge->SetupAttachment(Frame);
    LidHinge->SetRelativeLocation(FVector(0, -34, 18));
    LidMesh = MakePanel(TEXT("Lid"), LidHinge, FVector(0, 34, 7), FVector(45, 34, 7), Clue.Object, Crown.Object);
    ClaspMesh = MakePanel(TEXT("Clasp"), LidHinge, FVector(0, 68, 0), FVector(7.5, 2., 9.), Prize.Object, Hasp.Object);
    for (int32 I = 0; I < 2; ++I)
    {
        const double X = I ? 29. : -29.;
        MakePanel(I ? TEXT("StarboardHoop") : TEXT("PortHoop"), LidHinge,
            {X,34.,7.}, {3.5,34.6,7.65}, Iron.Object, Hoop.Object);
        for (int32 J = 0; J < 2; ++J)
        {
            const FName Name(*FString::Printf(TEXT("Band_%d_%d"), I, J));
            MakePanel(Name, Frame, {X,J ? 34. : -34.,-7.5}, {3.5,1.,23.7}, Iron.Object, Block.Object);
        }
    }
    MakePanel(TEXT("Keyhole"), LidHinge, {0,70.15,0}, {2.2,.35,4.1}, Iron.Object, Keyway.Object);
    CompassFace = Face.Object;
    PrizeMaterial = Prize.Object;
    ClueMaterial = Clue.Object;
}

void AIslandChest::BeginPlay()
{
    Super::BeginPlay();
    IslandSceneryVoxels::Register(*this);
}

void AIslandChest::SetPrize(bool bValue)
{
    bPrize = bValue;
    OnRep_Prize();
}

void AIslandChest::SeatAtReveal(const AVoxelTerrain& Terrain)
{
    auto* Body = Cast<UIslandMeshVoxels>(IslandSceneryVoxels::SurfaceFor(BoxMesh));
    const FVoxelDensity* Soil = Terrain.GetDensity();
    if (!HasAuthority() || !Body || !Soil) return;
    TMap<UIslandMeshVoxels*,TArray<int32>> Unsupported;
    IslandScenerySupport::InspectActor(*this, Terrain, Unsupported);
    const auto* LooseBody = Unsupported.Find(Body);
    if (!LooseBody || LooseBody->Num() < Body->RemainingCount()) return;

    // Use the bottom cell of each board column. A cave roof above the find
    // is irrelevant when looking for the first floor under the exposed box.
    FVector A,B,C;
    Body->GetCellAxes(A,B,C);
    const double HalfHeight = FMath::Abs(A.Z) + FMath::Abs(B.Z) + FMath::Abs(C.Z);
    TMap<FIntPoint,FVector> Sole;
    Body->ForEachSolid([&](int32,FVector Center)
    {
        Center.Z -= HalfHeight;
        const FIntPoint Column(FMath::RoundToInt(Center.X), FMath::RoundToInt(Center.Y));
        FVector& Feet = Sole.FindOrAdd(Column,Center);
        Feet.Z = FMath::Min(Feet.Z,Center.Z);
    });
    const auto TouchesSoil = [&](double Drop)
    {
        for (const auto& Sample : Sole)
            if (Soil->SignedDistanceAt(Sample.Value-FVector(0.,0.,Drop)) <= 0.) return true;
        return false;
    };
    constexpr double Step = VoxelPlayground::Spacing * .5;
    const double Limit = GetActorLocation().Z - Soil->Grid.Origin.Z;
    for (double Drop=Step; Drop <= Limit+Step; Drop+=Step)
    {
        if (!TouchesSoil(Drop)) continue;
        double Lo=Drop-Step, Hi=Drop;
        while (Hi-Lo > 1.)
        {
            const double Mid=(Lo+Hi)*.5;
            if (TouchesSoil(Mid)) Hi=Mid;
            else Lo=Mid;
        }
        DiscoveryRise=Hi;
        SetReplicateMovement(true);
        SetActorLocation(GetActorLocation()-FVector(0.,0.,Hi),false,nullptr,ETeleportType::TeleportPhysics);
        return;
    }
    // A fully excavated shaft may have no floor. Keep its discovery and award
    // interaction available while the unsupported timber is cleared normally.
    IslandSceneryVoxels::CheckFoundations(*GetWorld(), GetComponentsBoundingBox(true));
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
    LidAngle = 90.;
    LidHinge->SetRelativeRotation(FRotator(0, 0, -LidAngle));
}

double AIslandChest::GetInteractionDistanceSquared(FVector Origin) const
{
    const FVector Center = GetActorLocation();
    return FMath::Min(FVector::DistSquared(Origin,Center), FVector::DistSquared(Origin,Center+FVector(0.,0.,DiscoveryRise)));
}

bool AIslandChest::IsReachableFrom(const AVoxelCharacter& Hero) const
{
    const FVector Origin = Hero.GetAimOrigin();
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(IslandReachableChest), false, &Hero);
    const auto Reach = [&](FVector Spot)
    {
        if (FVector::DistSquared(Origin,Spot) > FMath::Square(IslandRules::OpenReach)) return false;
        FHitResult Hit;
        return !GetWorld()->LineTraceSingleByChannel(Hit, Origin, Spot, ECC_Visibility, Query) || Hit.GetActor() == this;
    };
    const FVector Center = GetActorLocation();
    return Reach(Center) || (DiscoveryRise > 0. && Reach(Center+FVector(0.,0.,DiscoveryRise)));
}

void AIslandChest::OnRep_Prize()
{
    BoxMesh->SetMaterial(0, bPrize ? GildedLacquer : ClueMaterial);
    LidMesh->SetMaterial(0, bPrize ? GildedLacquer : ClueMaterial);
    ClaspMesh->SetMaterial(0, bPrize ? ClueMaterial : PrizeMaterial);
    IslandSceneryVoxels::RefreshPart(*BoxMesh);
    IslandSceneryVoxels::RefreshPart(*LidMesh);
    IslandSceneryVoxels::RefreshPart(*ClaspMesh);
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
    if (!IsPrizeRevealed() || Compass) return;
    Compass = NewObject<USceneComponent>(this,TEXT("PrizeCompass"));
    Compass->SetupAttachment(GetRootComponent());
    Compass->SetRelativeLocation(FVector(0,0,8));
    // Build every grid at full scale, including peers which join partway through the reveal.
    AddInstanceComponent(Compass);
    Compass->RegisterComponent();
    const auto Piece = [&](FName Name, UStaticMesh* Shape, UMaterialInterface* Surface, FVector At, FRotator Facing, FVector Size)
    {
        auto* Part = NewObject<UStaticMeshComponent>(this, Name);
        Part->ComponentTags.Add(TEXT("IslandPrizeCompass"));
        Part->SetNetAddressable();
        Part->SetupAttachment(Compass);
        Part->SetReceivesDecals(false);
        Part->SetCollisionResponseToAllChannels(ECR_Ignore);
        Part->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Part->SetCanEverAffectNavigation(false);
        Part->SetStaticMesh(Shape);
        Part->SetMaterial(0, Surface);
        Part->SetRelativeTransform(FTransform(Facing, At, Size/100.));
        AddInstanceComponent(Part);
        Part->RegisterComponent();
    };
    Piece(TEXT("CompassRim"), CoinShape, PrizeMaterial, FVector::ZeroVector, FRotator(90,0,0), FVector(52,52,6));
    for (double Side : {-1.,1.})
    {
        Piece(Side < 0. ? TEXT("DialBack") : TEXT("DialFront"), DialShape, CompassFace, FVector(Side*3.4,0,0), FRotator(90,0,0), FVector(45,45,1));
        for (int32 I=0; I<4; ++I)
        {
            const FRotator NeedleRotation(0,0,I*90.);
            const FVector Along=NeedleRotation.RotateVector(FVector::UpVector);
            const FName Name(*FString::Printf(TEXT("CompassPoint_%d_%d"),Side<0. ? 0 : 1,I));
            Piece(Name, NeedleShape, I == 0 ? CompassFace : PrizeMaterial, FVector(Side*5.,0,0)+Along*(I == 0 ? 10.5 : 8.5), NeedleRotation, FVector(5,11,I == 0 ? 25 : 20));
        }
    }
    IslandSceneryVoxels::Register(*this);
    Compass->SetRelativeScale3D(FVector(.05));
    if (GetNetMode() != NM_DedicatedServer)
    {
        auto* Glow = NewObject<UPointLightComponent>(this,TEXT("CompassGlow"));
        Glow->SetupAttachment(Compass);
        Glow->SetLightColor(FLinearColor(1.f,.65f,.2f));
        Glow->SetIntensity(55.f);
        Glow->SetAttenuationRadius(170.f);
        Glow->SetCastShadows(false);
        Glow->RegisterComponent();
    }
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
    else if (bChallengeResolved)
    {
        Target = 90.;
        SetActorTickEnabled(false);
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
    DOREPLIFETIME(AIslandChest, DiscoveryRise);
}

AIslandWater::AIslandWater()
{
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Lagoon(TEXT("/Game/TreasureIsland/Art/M_Lagoon"));
    LagoonMaterial = Lagoon.Object;
    WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Sea"));
    SetRootComponent(WaterMesh);
    WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WaterMesh->SetCastShadow(false);
    WaterMesh->SetReceivesDecals(false);
    WaterMesh->SetCanEverAffectNavigation(false);
}

void AIslandWater::BeginPlay()
{
    Super::BeginPlay();
    const AVoxelTerrain* Terrain = AVoxelTerrain::Find(GetWorld());
    const TArray<FVector2D> Shore = VoxelPlayground::CoastOutline(Terrain ? Terrain->Layout : EVoxelLayout::Island);
    // Follow the shore outline, with shallow glass-green water, a teal shelf and deep water offshore.
    // Extend the deep-water colour past the horizon seen from the island overlook.
    // Keeping the previous outer ring preserves the colour gradient near shore.
    const FColor DeepWater(41,78,99);
    const double Bands[] = {1., 1.07, 1.22, 1.48, 18., 256.};
    const FColor Tints[] = {{149,202,172}, {101,181,166}, {61,150,151}, {45,117,135}, DeepWater, DeepWater};
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FLinearColor> Colors;
    TArray<FVector2D> CoastUV;
    for (int32 Ring=0; Ring<UE_ARRAY_COUNT(Bands); ++Ring)
    {
        for (const FVector2D& Point : Shore)
        {
            Vertices.Emplace(Point*Bands[Ring],0.);
            Colors.Emplace(Tints[Ring]);
            CoastUV.Emplace((Bands[Ring]-1.)*Point.Size(),0.);
        }
    }
    const int32 Count = Shore.Num();
    for (int32 Ring=0; Ring+1<UE_ARRAY_COUNT(Bands); ++Ring)
        for (int32 Side=0; Side<Count; ++Side)
        {
            const int32 A=Ring*Count+Side, B=Ring*Count+(Side+1)%Count;
            Triangles.Append({A,B,B+Count,A,B+Count,A+Count});
        }
    WaterMesh->CreateMeshSection_LinearColor(0,Vertices,Triangles,{},CoastUV,Colors,{},false);
    if (LagoonMaterial && (!WaterMaterial || WaterMaterial->GetPathName() == TEXT("/Game/TreasureIsland/M_Sea.M_Sea") || WaterMaterial == LagoonMaterial))
    {
        UMaterialInstanceDynamic* Lagoon = UMaterialInstanceDynamic::Create(LagoonMaterial, this);
        Lagoon->SetScalarParameterValue(TEXT("UseCoastColors"),1.f);
        WaterMesh->SetMaterial(0,Lagoon);
    }
    else WaterMesh->SetMaterial(0,WaterMaterial);
}
