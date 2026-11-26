#include "Island/ExpeditionMudBall.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameStateBase.h"
#include "Island/ExpeditionAnimalComponent.h"
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
}

AExpeditionMudBall::AExpeditionMudBall()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    SetReplicateMovement(false);
    PrimaryActorTick.bCanEverTick = true;
    Clod = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlyingClay"));
    SetRootComponent(Clod);
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
    ApplyLaunchPose();
    ForceNetUpdate();
}

FVector AExpeditionMudBall::LaunchVelocity(const FVector& Aim)
{
    return Aim * ThrowSpeed + FVector::UpVector * (ArcLift * Aim.Size2D());
}

void AExpeditionMudBall::ApplyLaunchPose()
{
    if (!bPlayedThrow && GetInstigator() && Flight.LaunchedAt >= 0.)
    {
        if (auto* Animal = GetInstigator()->FindComponentByClass<UExpeditionAnimalComponent>())
            Animal->PlayThrow();
        bPlayedThrow = true;
    }
}

FVector AExpeditionMudBall::PositionAt(double Age) const
{
    return Flight.Start + Flight.Velocity * Age - FVector::UpVector * (.5 * FallAcceleration * Age * Age);
}

bool AExpeditionMudBall::TraceSegment(const FVector& Start, const FVector& End, FHitResult& Hit) const
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(ExpeditionMud), false, GetInstigator());
    const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
    TArray<FHitResult> Contacts;
    GetWorld()->SweepMultiByChannel(Contacts, Start, End, FQuat::Identity, ECC_Visibility, Shape, Query);
    // Thin timbers and canvas are query touches, so that visibility rays can pass
    // through them. Clay still lands on the first real voxel surface it contacts.
    const FHitResult* WorldHit = Contacts.FindByPredicate([](const FHitResult& Contact)
    {
        return Contact.bBlockingHit || Cast<AIslandNeutralProp>(Contact.GetActor())
            || AIslandAttachedMud::CanReceive(Contact.GetComponent());
    });
    const bool bWorld = WorldHit != nullptr;
    if (WorldHit) Hit = *WorldHit;
    // Pawn capsules intentionally ignore visibility: digging and opening chests still work in a crowd.
    // Compare both continuous sweeps; a wall wins ties with a capsule touching its far side.
    FHitResult PawnHit;
    const FCollisionObjectQueryParams Pawns(ECC_Pawn);
    if (GetWorld()->SweepSingleByObjectType(PawnHit, Start, End, FQuat::Identity, Pawns, Shape, Query)
        && Cast<AVoxelCharacter>(PawnHit.GetActor()) && (!bWorld || PawnHit.Time < Hit.Time - KINDA_SMALL_NUMBER))
    {
        Hit = PawnHit;
        return true;
    }
    return bWorld;
}

void AExpeditionMudBall::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Flight.LaunchedAt < 0.) return;
    ApplyLaunchPose();
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
            if (!TraceSegment(From, PositionAt(Next), Hit)) continue;
            Stop.Time = Now;
            Stop.Point = Hit.bStartPenetrating ? From : Hit.ImpactPoint;
            Stop.Normal = Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
            AIslandGameMode* Mode = GetWorld()->GetAuthGameMode<AIslandGameMode>();
            AIslandPlayerController* Thrower = Cast<AIslandPlayerController>(GetOwner());
            if (Mode && Thrower) Stop.MarkedCharacter = Mode->ResolveMudImpact(*Thrower, Hit, Stop.Marks);
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
    SetActorLocationAndRotation(PositionAt(Age), (Flight.Velocity - FVector::UpVector * (FallAcceleration * Age)).Rotation());
}

void AExpeditionMudBall::DrawStop(double Now)
{
    if (!bPlayedMarks && Stop.MarkedCharacter)
    {
        if (auto* Animal = Stop.MarkedCharacter->FindComponentByClass<UExpeditionAnimalComponent>())
            Animal->SetMarks(Stop.Marks);
        bPlayedMarks = true;
    }
    const double Fraction = FMath::Clamp((Now - Stop.Time) / SplatDuration, 0., 1.);
    // Every contact leaves the same brief splash at the first impact. Nothing follows a buried target.
    SetActorLocationAndRotation(Stop.Point + Stop.Normal * 1.5, FRotationMatrix::MakeFromZ(Stop.Normal).Rotator());
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
