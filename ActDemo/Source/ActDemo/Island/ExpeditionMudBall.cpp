#include "Island/ExpeditionMudBall.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Island/ExpeditionAnimalComponent.h"
#include "Island/ExpeditionBodyMud.h"
#include "Island/IslandAttachedMud.h"
#include "Island/IslandNeutralProp.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandPlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/VoxelCharacter.h"

namespace
{
    constexpr double MaxFlightAge = 1.3;
    constexpr double SplatDuration = .24;

    bool TraceMudSurface(UWorld& World, const FVector& Start, const FVector& End, float SweepRadius,
        const FCollisionQueryParams& Query, FHitResult& Hit)
    {
        TArray<FHitResult> Contacts;
        if (SweepRadius > 0.f)
            World.SweepMultiByChannel(Contacts, Start, End, FQuat::Identity, ECC_Visibility,
                FCollisionShape::MakeSphere(SweepRadius), Query);
        else
            World.LineTraceMultiByChannel(Contacts, Start, End, ECC_Visibility, Query);
        for (const FHitResult& Contact : Contacts)
        {
            if (!Contact.bBlockingHit && !Cast<AIslandNeutralProp>(Contact.GetActor())
                && !AIslandAttachedMud::CanReceive(Contact.GetComponent())) continue;
            Hit = Contact;
            // Initial sphere overlaps report the sweep origin as the impact. Recover
            // the surface position, otherwise the pad's short support trace can miss.
            if (Hit.bStartPenetrating && SweepRadius > 0.f && !Hit.ImpactNormal.IsNearlyZero())
                Hit.ImpactPoint = Start - Hit.ImpactNormal * (SweepRadius - Hit.PenetrationDepth);
            return true;
        }
        return false;
    }
}

AExpeditionMudBall::AExpeditionMudBall()
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

void AExpeditionMudBall::BeginPlay()
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

double AExpeditionMudBall::ServerNow() const
{
    const AGameStateBase* State = GetWorld()->GetGameState();
    return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void AExpeditionMudBall::Launch(const FVector& Aim)
{
    Flight.Start = GetActorLocation();
    Flight.Velocity = LaunchVelocity(Aim);
    Flight.LaunchedAt = ServerNow();
    UpdateFlyingClod(0.);
    ForceNetUpdate();
}

FVector AExpeditionMudBall::LaunchVelocity(const FVector& Aim)
{
    return Aim * ThrowSpeed + FVector::UpVector * (ArcLift * Aim.Size2D());
}

FVector AExpeditionMudBall::PositionAt(double Age) const
{
    return Flight.Start + Flight.Velocity * Age - FVector::UpVector * (.5 * FallAcceleration * Age * Age);
}

FVector AExpeditionMudBall::AimAtPoint(const FVector& From, const FVector& BallCenter)
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

bool AExpeditionMudBall::TraceContact(UWorld& World, const AVoxelCharacter& Thrower, const FVector& Start,
    const FVector& End, float SweepRadius, FHitResult& Hit)
{
    const FCollisionQueryParams Query(SCENE_QUERY_STAT(ExpeditionMud), false, &Thrower);
    FCollisionQueryParams SceneryQuery = Query;
    FHitResult Body;
    bool bBody = false;
    const FBox FlightBounds = FBox(Start.ComponentMin(End),Start.ComponentMax(End)).ExpandBy(SweepRadius + 1.);
    // Only a character's physical capsule and its actual clods can catch a throw.
    // Cosmetic and interaction components on the Pawn channel can extend far outside it.
    for (TActorIterator<AVoxelCharacter> It(&World); It; ++It)
    {
        if (*It == &Thrower) continue;
        SceneryQuery.AddIgnoredActor(*It);
        auto* Capsule = It->GetCapsuleComponent();
        FHitResult Contact;
        if (Capsule->IsQueryCollisionEnabled() && FlightBounds.Intersect(Capsule->Bounds.GetBox())
            && (SweepRadius > 0.f
                ? Capsule->SweepComponent(Contact,Start,End,FQuat::Identity,FCollisionShape::MakeSphere(SweepRadius))
                : Capsule->LineTraceComponent(Contact,Start,End,Query))
            && (!bBody || Contact.Time < Body.Time))
        {
            if (Contact.bStartPenetrating)
            {
                const double BodyRadius = Capsule->GetScaledCapsuleRadius();
                const FVector Offset = Capsule->GetUpVector() * (Capsule->GetScaledCapsuleHalfHeight() - BodyRadius);
                const FVector Center = Capsule->GetComponentLocation();
                const FVector Axis = FMath::ClosestPointOnSegment(Start, Center - Offset, Center + Offset);
                Contact.ImpactNormal = (Start - Axis).GetSafeNormal(UE_SMALL_NUMBER,
                    (Start - End).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector));
                Contact.ImpactPoint = Axis + Contact.ImpactNormal * BodyRadius;
            }
            Contact.Item = INDEX_NONE;
            Body = Contact;
            bBody = true;
        }
        const auto* Mud = It->GetBodyMud();
        FHitResult Clay;
        if (Mud && Mud->Trace(Start, End, SweepRadius, Clay) && (!bBody || Clay.Time <= Body.Time))
        {
            Body = Clay;
            bBody = true;
        }
    }
    FHitResult Surface;
    const bool bSurface = TraceMudSurface(World, Start, End, SweepRadius, SceneryQuery, Surface);
    if (bBody && bSurface)
    {
        // Physics pulls a sweep hit back a small distance. For grazing both surfaces,
        // compare at centimetre precision and check the exposed side at the actual
        // contact, so a floor-level ankle wins but a capsule behind a wall never does.
        const double Along = (Body.Time - Surface.Time) * FVector::Dist(Start, End);
        if (Along > .8) bBody = false;
        else if (Along >= -.8)
        {
            FHitResult Occluder;
            const bool bBehind = FVector::DotProduct(Body.ImpactPoint - Surface.ImpactPoint, Surface.ImpactNormal) < -.5;
            const bool bOccluded = TraceMudSurface(World, Start, Body.ImpactPoint, 0.f, SceneryQuery, Occluder)
                && FVector::DistSquared(Occluder.ImpactPoint, Body.ImpactPoint) > .25;
            bBody = !bBehind && !bOccluded;
        }
    }
    if (bBody) Hit = Body;
    else if (bSurface) Hit = Surface;
    return bBody || bSurface;
}

void AExpeditionMudBall::Tick(float DeltaSeconds)
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
            auto* Source = Cast<AVoxelCharacter>(GetInstigator());
            if (!Source || !TraceContact(*GetWorld(), *Source, From, PositionAt(Next), Radius, Hit)) continue;
            Stop.Time = Now;
            Stop.Point = Hit.ImpactPoint;
            Stop.Normal = Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
            AIslandGameMode* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>();
            AIslandPlayerController* Thrower = Cast<AIslandPlayerController>(GetOwner());
            if (Mode && Thrower) Mode->ResolveMudImpact(*Thrower, Hit);
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

void AExpeditionMudBall::UpdateFlyingClod(double Age)
{
    // The sweep follows the reticle from frame one. The visible clay leaves the palm and joins that arc.
    if (GetNetMode() == NM_DedicatedServer) return;
    constexpr double HandBlendSeconds = .13;
    if (!HandOffset.IsSet() && Age < HandBlendSeconds && GetInstigator())
    {
        const auto* Animal = GetInstigator()->FindComponentByClass<UExpeditionAnimalComponent>();
        FVector Hand;
        if (Animal && Animal->GetThrowHandLocation(Hand))
            HandOffset = Hand - FVector(Flight.Start);
    }
    if (HandOffset.IsSet())
        Clod->SetWorldLocation(GetActorLocation() + HandOffset.GetValue() * FMath::Square(FMath::Max(0., 1. - Age / HandBlendSeconds)));
    Clod->SetVisibility(true);
}

void AExpeditionMudBall::DrawStop(double Now)
{
    const double Fraction = FMath::Clamp((Now - Stop.Time) / SplatDuration, 0., 1.);
    SetActorLocationAndRotation(Stop.Point + Stop.Normal * 1.5, FRotationMatrix::MakeFromZ(Stop.Normal).Rotator());
    Clod->SetRelativeLocation(FVector::ZeroVector);
    const double Width = (.22 + Fraction * .28) * (1. - FMath::Pow(Fraction, 4.));
    Clod->SetWorldScale3D(FVector(Width, Width, .035 * (1. - Fraction)));
    Clod->SetVisibility(Fraction < 1.);
}

void AExpeditionMudBall::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(AExpeditionMudBall, Flight, COND_InitialOnly);
    DOREPLIFETIME(AExpeditionMudBall, Stop);
}
