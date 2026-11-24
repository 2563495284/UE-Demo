#include "Island/IslandNeutralProp.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandPlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
    constexpr double TreeLandingTime = .95;
    constexpr double TidePeriod = 3.6;
    constexpr double MouthY = 150.;
}

AIslandNeutralProp::AIslandNeutralProp()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Origin")));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BoxAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> TubeAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    Cube = BoxAsset.Object;
    Cylinder = TubeAsset.Object;
    Ball = SphereAsset.Object;
    PaintBase = Material.Object;
}

void AIslandNeutralProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AIslandNeutralProp, State);
}

void AIslandNeutralProp::BeginPlay()
{
    Super::BeginPlay();
    OnRep_State();
}

double AIslandNeutralProp::ServerNow() const
{
    const AGameStateBase* GS = GetWorld()->GetGameState();
    return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void AIslandNeutralProp::Initialize(EIslandNeutralKind Kind)
{
    if (!HasAuthority() || State.bInitialized) return;
    State.Kind = Kind;
    State.bInitialized = true;
    // The first sight of a rolled sail is fully at rest. Subsequent rope pulls
    // interpolate both directions from the timestamp shared by the server.
    State.ChangedAt = ServerNow() - (Kind == EIslandNeutralKind::Sail ? .5 : 0.);
    if (Kind == EIslandNeutralKind::Tide)
    {
        const AVoxelTerrain* Terrain = AVoxelTerrain::Find(GetWorld());
        if (const FVoxelDensity* Density = Terrain ? Terrain->GetDensity() : nullptr)
        {
            const auto MouthOffset = [&](int32 Index)
            {
                const FVector Center = GetTideMouth(Index);
                return static_cast<int16>(FMath::RoundToInt(Density->SurfaceAt(FVector2D(Center)).Get(GetActorLocation().Z) - GetActorLocation().Z));
            };
            State.MouthOffsetA = MouthOffset(0);
            State.MouthOffsetB = MouthOffset(1);
        }
    }
    OnRep_State();
    ForceNetUpdate();
}

void AIslandNeutralProp::OnRep_State()
{
    if (!State.bInitialized || !GetWorld()) return;
    if (!bBuilt)
    {
        SetActorScale3D(IslandNeutral::SiteScale(State.Kind));
        switch (State.Kind)
        {
            case EIslandNeutralKind::Tree: BuildTree(); break;
            case EIslandNeutralKind::Ship: BuildShip(); break;
            case EIslandNeutralKind::Hopper: BuildHopper(); break;
            case EIslandNeutralKind::Sail: BuildSail(); break;
            case EIslandNeutralKind::Tide: BuildTide(); break;
        }
        bBuilt = true;
    }
    UpdateVisuals(ServerNow());
    SetActorTickEnabled(State.Kind == EIslandNeutralKind::Tide || State.Kind == EIslandNeutralKind::Sail
        || (State.Kind == EIslandNeutralKind::Tree && (State.Phase == 1 || State.Phase == 2))
        || (State.Kind == EIslandNeutralKind::Hopper && State.Phase == 1));
    if (ClothVolume && HasAuthority())
        ClothVolume->SetCollisionEnabled(State.Phase == 1 ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

void AIslandNeutralProp::SetPhase(uint8 Next)
{
    State.Phase = Next;
    State.ChangedAt = ServerNow();
    OnRep_State();
    ForceNetUpdate();
}

FVector AIslandNeutralProp::GetInteractionPoint() const
{
    switch (State.Kind)
    {
        case EIslandNeutralKind::Tree: return GetActorTransform().TransformPosition(FVector(-55,0,64));
        case EIslandNeutralKind::Hopper: return GetActorTransform().TransformPosition(FVector(-95,-157,108));
        case EIslandNeutralKind::Sail: return GetActorTransform().TransformPosition(FVector(-30,-169,82));
        case EIslandNeutralKind::Tide: return GetTideMouth(0);
        default: return GetActorLocation();
    }
}

bool AIslandNeutralProp::Strike(AIslandPlayerController& Player, const FHitResult& Hit)
{
    if (!HasAuthority() || !bBuilt || !HitTarget || Hit.GetComponent() != HitTarget || !Player.GetHero()) return false;
    if (State.Kind == EIslandNeutralKind::Tree && State.Phase < 2)
    {
        SetPhase(State.Phase + 1);
        return true;
    }
    if (State.Kind == EIslandNeutralKind::Hopper && State.Phase == 0)
    {
        bSandQueued = false;
        SetPhase(1);
        return true;
    }
    return false;
}

bool AIslandNeutralProp::CanUseRope(const AIslandPlayerController& Player) const
{
    const AVoxelCharacter* Hero = Player.GetHero();
    if (State.Kind != EIslandNeutralKind::Sail || !Hero || !bBuilt) return false;
    const FVector Aim = Hero->GetAimOrigin();
    const FVector Knot = GetInteractionPoint();
    if (FVector::DistSquared(Aim, Knot) > FMath::Square(200.)) return false;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(NeutralRope), false, Hero);
    return !GetWorld()->LineTraceSingleByChannel(Hit, Aim, Knot, ECC_Visibility, Params)
        || Hit.GetComponent() == HitTarget;
}

bool AIslandNeutralProp::UseRope(AIslandPlayerController& Player)
{
    if (!HasAuthority() || !CanUseRope(Player) || ServerNow() - State.ChangedAt < .38) return false;
    SetPhase(State.Phase == 0 ? 1 : 0);
    return true;
}

bool AIslandNeutralProp::AnySailObscures(UWorld& World, const FVector& Start, const FVector& End)
{
    for (TActorIterator<AIslandNeutralProp> It(&World); It; ++It)
    {
        if (It->GetKind() != EIslandNeutralKind::Sail) continue;
        // Check the current cloth panels in mesh space, including their movement as a pawn crosses.
        for (int32 I = 0; I < It->Cloth.Num(); I += 2)
        {
            const FTransform& Panel = It->Cloth[I]->GetComponentTransform();
            const FVector A = Panel.InverseTransformPosition(Start);
            const FVector B = Panel.InverseTransformPosition(End);
            const FBox Bounds = It->Cloth[I]->GetStaticMesh()->GetBoundingBox();
            if (FMath::LineBoxIntersection(Bounds,A,B,B-A)) return true;
        }
    }
    return false;
}

void AIslandNeutralProp::TouchSail(UPrimitiveComponent* Overlap, AActor* Other, UPrimitiveComponent* OtherComponent,
    int32 BodyIndex, bool bSwept, const FHitResult& Sweep)
{
    if (!HasAuthority() || State.Phase != 1 || !Cast<AVoxelCharacter>(Other)) return;
    const double Y = GetActorTransform().InverseTransformPosition(Other->GetActorLocation()).Y;
    State.RippleSide = Y < -57. ? -1 : (Y > 57. ? 1 : 0);
    State.RippleAt = ServerNow();
    SetActorTickEnabled(true);
    ForceNetUpdate();
}

FVector AIslandNeutralProp::GetTideMouth(int32 Index) const
{
    return GetActorTransform().TransformPosition(FVector(0, Index == 0 ? -MouthY : MouthY, 0.))
        + FVector::UpVector * (6. + (Index == 0 ? State.MouthOffsetA : State.MouthOffsetB));
}

void AIslandNeutralProp::RefreshTide(const AVoxelTerrain& Terrain)
{
    const FVoxelDensity* Density = Terrain.GetDensity();
    if (!HasAuthority() || State.Kind != EIslandNeutralKind::Tide || !Density) return;
    uint8 Mask = 0;
    constexpr double D = 40.;
    const FVector2D Offsets[] = {{0,0},{D,0},{-D,0},{0,D},{0,-D}};
    for (int32 Mouth = 0; Mouth < 2; ++Mouth)
    {
        int32 SolidSamples = 0;
        for (const FVector2D Offset : Offsets)
        {
            const FVector LocalOffset = GetActorQuat().RotateVector(FVector(Offset, 20.));
            const FVector GridPos = (GetTideMouth(Mouth) + LocalOffset - FVector(Density->Grid.Origin)) / VoxelPlayground::Spacing;
            const FIntVector Cell(FMath::RoundToInt(GridPos.X), FMath::RoundToInt(GridPos.Y), FMath::RoundToInt(GridPos.Z));
            if (Density->Grid.Contains(Cell) && Density->Sample(Cell) <= 0) ++SolidSamples;
        }
        if (SolidSamples >= 3) Mask |= 1u << Mouth;
    }
    if (State.PlugMask != Mask)
    {
        State.PlugMask = Mask;
        State.ChangedAt = ServerNow();
        OnRep_State();
        ForceNetUpdate();
    }
}

double AIslandNeutralProp::TideCycle(double Now) const
{
    return FMath::Fmod(FMath::Max(0., Now - State.ChangedAt), TidePeriod);
}

bool AIslandNeutralProp::IsTideBoostReady() const
{
    const double Cycle = TideCycle(ServerNow());
    return (State.PlugMask == 1 || State.PlugMask == 2) && Cycle >= .36 && Cycle <= 2.;
}

bool AIslandNeutralProp::TryTideJump(AVoxelCharacter& Hero)
{
    if (!HasAuthority() || State.Kind != EIslandNeutralKind::Tide || !IsTideBoostReady()) return false;
    const FVector Mouth = GetTideMouth(State.PlugMask == 1 ? 1 : 0);
    const FVector Foot = Hero.GetActorLocation() - FVector::UpVector * Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if (FVector::DistSquared2D(Mouth, Foot) > FMath::Square(IslandNeutral::TideRadius) ||
        Foot.Z < Mouth.Z - 90. || Foot.Z > Mouth.Z + 125.) return false;
    const FVector Target = Mouth + GetActorForwardVector() * IslandNeutral::TideTravel;
    const AVoxelTerrain* Terrain = AVoxelTerrain::Find(GetWorld());
    const FVoxelDensity* Density = Terrain ? Terrain->GetDensity() : nullptr;
    const double Ground = Density ? Density->SurfaceAt(FVector2D(Target)).Get(Mouth.Z) : Mouth.Z;
    constexpr double UpSpeed = 920.;
    const double Gravity = FMath::Max(1., double(-Hero.GetCharacterMovement()->GetGravityZ()));
    const double Flight = (UpSpeed + FMath::Sqrt(FMath::Max(0., UpSpeed*UpSpeed - 2.*Gravity*(Ground-Foot.Z)))) / Gravity;
    const FVector Horizontal = FVector(Target.X-Foot.X, Target.Y-Foot.Y, 0.) / Flight;
    Hero.LaunchCharacter(Horizontal.GetClampedToMaxSize(750.) + FVector::UpVector * UpSpeed, true, true);
    return true;
}

TArray<FVector> AIslandNeutralProp::GetSandCenters() const
{
    // Adjacent 75 cm sphere brushes overlap in three dimensions. Columns below
    // the walk surface join the existing earth instead of leaving a suspended ramp.
    TArray<FVector> Centers;
    const AVoxelTerrain* Terrain = AVoxelTerrain::Find(GetWorld());
    const FVoxelDensity* Density = Terrain ? Terrain->GetDensity() : nullptr;
    const FTransform& Frame = GetActorTransform();
    for (int32 Step = 0; Step < 10; ++Step)
    {
        const double X = 25. + 37.5 * Step;
        const double Top = -38. + 14.5 * Step;
        for (double Y : {-48., 0., 48.})
        {
            FVector Center = Frame.TransformPosition(FVector(X,Y,Top));
            const double Ground = Density ? Density->SurfaceAt(FVector2D(Center)).Get(Center.Z-125.) : Center.Z-125.;
            Center.Z = FMath::Min(Center.Z,Ground+125.);
            Centers.Add(Center);
            if (Center.Z-Ground > 50.) Centers.Add(Center-FVector::UpVector*82.);
        }
    }
    return Centers;
}

void AIslandNeutralProp::CompleteSandRelease(bool bCommitted)
{
    if (!HasAuthority() || State.Kind != EIslandNeutralKind::Hopper || State.Phase != 1) return;
    bSandQueued = false;
    SetPhase(bCommitted ? 2 : 0);
}

bool AIslandNeutralProp::IsTreeLandingClear() const
{
    FCollisionObjectQueryParams Pawns(ECC_Pawn);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(NeutralTreeLanding), false, this);
    for (const UStaticMeshComponent* Mesh : TreeSolids)
    {
        const FVector Half = Mesh->GetStaticMesh()->GetBoundingBox().GetExtent() * Mesh->GetComponentScale().GetAbs();
        if (GetWorld()->OverlapAnyTestByObjectType(Mesh->GetComponentLocation(), Mesh->GetComponentQuat(), Pawns,
            FCollisionShape::MakeBox(Half), Query))
            return false;
    }
    return true;
}

void AIslandNeutralProp::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Now = ServerNow();
    UpdateVisuals(Now);
    const double Age = Now - State.ChangedAt;
    if ((State.Kind == EIslandNeutralKind::Tree && State.Phase == 1 && Age >= 2.5)
        || (State.Kind == EIslandNeutralKind::Sail && Age >= .36 && Now - State.RippleAt >= .68))
        SetActorTickEnabled(false);
    if (!HasAuthority()) return;
    if (State.Kind == EIslandNeutralKind::Tree && State.Phase == 2 && Age >= TreeLandingTime)
    {
        // The falling wood is visual-only. Switch on the settled collision as a
        // single step after this frame's final placement is clear of all capsules.
        if (IsTreeLandingClear()) SetPhase(3);
    }
    else if (State.Kind == EIslandNeutralKind::Hopper && State.Phase == 1 && Age > .48 && !bSandQueued)
    {
        bSandQueued = true;
        if (AIslandGameMode* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>())
            Mode->QueueHopperSand(*this, GetSandCenters());
        else
            CompleteSandRelease(false);
    }
}

void AIslandNeutralProp::UpdateVisuals(double Now)
{
    const double Age = FMath::Max(0., Now - State.ChangedAt);
    switch (State.Kind)
    {
        case EIslandNeutralKind::Tree: AnimateTree(Age); break;
        case EIslandNeutralKind::Hopper: AnimateHopper(Age); break;
        case EIslandNeutralKind::Sail: AnimateSail(Now); break;
        case EIslandNeutralKind::Tide: AnimateTide(Now); break;
        default: break;
    }
}
