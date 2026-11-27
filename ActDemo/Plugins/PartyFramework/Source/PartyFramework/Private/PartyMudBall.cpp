#include "PartyMudBall.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "PartyVoxelArena.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr double MaxFlightAge = 1.3;
    constexpr double SplatDuration = .24;
}

APartyMudBall::APartyMudBall()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(false);
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("FlightPath")));
    Clod = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlyingClay"));
    Clod->SetupAttachment(GetRootComponent());
    Clod->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Clod->SetGenerateOverlapEvents(false);
    Clod->SetCastShadow(true);
    Clod->SetReceivesDecals(false);
    Clod->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Game/TreasureIsland/Art/Neutral/SM_ReefStone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Clay(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    Clod->SetStaticMesh(Sphere.Object);
    ClayBase = Clay.Object;
}

void APartyMudBall::BeginPlay()
{
    Super::BeginPlay();
    Clod->SetVisibility(false);
    Clod->SetWorldScale3D(FVector(.24,.19,.18));
    if (GetNetMode() != NM_DedicatedServer)
    {
        if (UMaterialInstanceDynamic* Clay = UMaterialInstanceDynamic::Create(ClayBase, this))
        {
            Clay->SetVectorParameterValue(TEXT("Color"), FLinearColor(FColor(181,109,64)));
            Clod->SetMaterial(0, Clay);
        }
    }
}

double APartyMudBall::ServerNow() const
{
    const AGameStateBase* State = GetWorld()->GetGameState();
    return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void APartyMudBall::Launch(const FVector& Aim)
{
    Flight.Start = GetActorLocation();
    Flight.Velocity = LaunchVelocity(Aim);
    Flight.LaunchedAt = ServerNow();
    UpdateFlyingClod(0.);
    ForceNetUpdate();
}

FVector APartyMudBall::LaunchVelocity(const FVector& Aim)
{
    return Aim * ThrowSpeed + FVector::UpVector * (ArcLift * Aim.Size2D());
}

FVector APartyMudBall::PositionAt(double Age) const
{
    return Flight.Start + Flight.Velocity * Age - FVector::UpVector * (.5 * FallAcceleration * Age * Age);
}

FVector APartyMudBall::AimAtPoint(const FVector& From, const FVector& BallCenter)
{
    const FVector Delta = BallCenter - From;
    const double Run = Delta.Size2D();
    if (Run < 1.) return Delta.GetSafeNormal();
    const double K = FallAcceleration * Run / (2. * FMath::Square(ThrowSpeed));
    const double C = Delta.Z / Run - ArcLift / ThrowSpeed + K;
    const double Discriminant = 1. - 4. * K * C;
    if (Discriminant < 0.) return Delta.GetSafeNormal();
    const double Tangent = 2. * C / (1. + FMath::Sqrt(Discriminant));
    return (Delta.GetSafeNormal2D() + FVector::UpVector * Tangent).GetSafeNormal();
}

FVector APartyMudBall::AimAtSurface(const FVector& From, const FVector& SightDirection, const FHitResult& Hit)
{
    const FVector BallCenter = Hit.ImpactPoint + Hit.ImpactNormal * Radius;
    const FVector ToSurface = Hit.ImpactPoint - From;
    const FVector ToCenter = BallCenter - From;
    // At a start-overlap the contact or its offset can be behind the thrower.
    // Throw into the surface so the first sweep can resolve it.
    if (FVector::DotProduct(ToCenter, SightDirection) <= 0. || FVector::DotProduct(ToSurface, SightDirection) <= 0.)
        return SightDirection;
    if (FVector::DotProduct(ToCenter, ToSurface) <= 0.)
        return ToSurface.GetSafeNormal(UE_SMALL_NUMBER, SightDirection);
    return AimAtPoint(From, BallCenter);
}

bool APartyMudBall::SweepFlight(const FVector& Start, const FVector& End, FHitResult& Hit) const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(PartyMudFlight), false, this);
    Query.AddIgnoredActor(GetInstigator());
    if (!GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility,
            FCollisionShape::MakeSphere(Radius), Query)) return false;
    if (Hit.bStartPenetrating && !Hit.ImpactNormal.IsNearlyZero())
        Hit.ImpactPoint = Start - Hit.ImpactNormal * (Radius - Hit.PenetrationDepth);
    return true;
}

void APartyMudBall::ApplyImpact(const FHitResult& Hit)
{
    auto* Thrower = Cast<APartyVoxelPawn>(GetInstigator());
    if (Thrower && Thrower->GetSession())
        Thrower->GetSession()->ResolveMudImpact(*Thrower, Hit);
}

bool APartyMudBall::GetThrowHandLocation(FVector& OutLocation) const
{
    const auto* Thrower = Cast<APartyVoxelPawn>(GetInstigator());
    return Thrower && Thrower->GetThrowHandLocation(OutLocation);
}

void APartyMudBall::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Flight.LaunchedAt < 0.) return;
    const double Now = ServerNow();
    if (Stop.Time >= 0.)
    {
        DrawStop(Now);
        return;
    }
    const double Age = FMath::Clamp(Now - Flight.LaunchedAt, 0., MaxFlightAge);
    if (HasAuthority())
    {
        while (SimulatedAge + UE_DOUBLE_SMALL_NUMBER < Age)
        {
            const double Next = FMath::Min(Age, SimulatedAge + 1./60.);
            FHitResult Hit;
            const FVector From = PositionAt(SimulatedAge);
            SimulatedAge = Next;
            if (!SweepFlight(From, PositionAt(Next), Hit)) continue;
            Stop.Time = Now;
            Stop.Point = Hit.ImpactPoint;
            Stop.Normal = Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
            ApplyImpact(Hit);
            ForceNetUpdate();
            DrawStop(Now);
            SetLifeSpan(SplatDuration + .15);
            return;
        }
        if (Age >= MaxFlightAge)
        {
            Destroy();
            return;
        }
    }
    const FVector At = PositionAt(Age);
    SetActorLocationAndRotation(At, (Flight.Velocity - FVector::UpVector * (FallAcceleration * Age)).Rotation());
    UpdateFlyingClod(Age);
}

void APartyMudBall::UpdateFlyingClod(double Age)
{
    // The sweep follows the reticle from frame one. The visible clay leaves the palm and joins that arc.
    if (GetNetMode() == NM_DedicatedServer) return;
    constexpr double HandBlendSeconds = .13;
    if (!HandOffset.IsSet() && Age < HandBlendSeconds && GetInstigator())
    {
        FVector Hand;
        if (GetThrowHandLocation(Hand))
            HandOffset = Hand - FVector(Flight.Start);
    }
    if (HandOffset.IsSet())
        Clod->SetWorldLocation(GetActorLocation() + HandOffset.GetValue() * FMath::Square(FMath::Max(0., 1. - Age / HandBlendSeconds)));
    Clod->SetVisibility(true);
}

void APartyMudBall::DrawStop(double Now)
{
    const double Fraction = FMath::Clamp((Now - Stop.Time) / SplatDuration, 0., 1.);
    SetActorLocationAndRotation(Stop.Point + Stop.Normal * 1.5, FRotationMatrix::MakeFromZ(Stop.Normal).Rotator());
    Clod->SetRelativeLocation(FVector::ZeroVector);
    const double Width = (.22 + Fraction * .28) * (1. - FMath::Pow(Fraction, 4.));
    Clod->SetWorldScale3D(FVector(Width, Width, .035 * (1. - Fraction)));
    Clod->SetVisibility(Fraction < 1.);
}

void APartyMudBall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(APartyMudBall, Flight, COND_InitialOnly);
    DOREPLIFETIME(APartyMudBall, Stop);
}
