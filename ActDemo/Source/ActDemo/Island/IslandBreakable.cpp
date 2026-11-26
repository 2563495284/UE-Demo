#include "Island/IslandBreakable.h"
#include "Island/IslandChipBurst.h"
#include "Island/IslandSceneryVoxels.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "Island/ExpeditionDock.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/App.h"
#include "Net/UnrealNetwork.h"
#include "RHIGlobals.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr double FallSeconds = .65;
    constexpr double FragmentSeconds = .5;
}

AIslandBreakable::AIslandBreakable()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;

    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Origin")));
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    Mesh->SetupAttachment(RootComponent);
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    Mesh->SetCanEverAffectNavigation(false);
    Crown = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ExampleTreeCrown"));
    Crown->SetupAttachment(Mesh);
    Crown->SetCollisionProfileName(TEXT("BlockAll"));
    Crown->SetCanEverAffectNavigation(false);
    Sign = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Sign"));
    Sign->SetupAttachment(RootComponent);
    Sign->SetText(FText::GetEmpty());
    Sign->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AIslandBreakable::SetUpExampleTree()
{
    Effect = EIslandBreakEffect::TreeFall;
    Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
    Mesh->SetRelativeLocation(FVector(0, 0, 140));
    Mesh->SetRelativeScale3D(FVector(.42, .42, 2.8));
    Crown->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
    Crown->SetRelativeLocation(FVector(0, 0, .46 * 100));
    Crown->SetRelativeScale3D(FVector(4.6, 4.6, .55));
}

void AIslandBreakable::BeginPlay()
{
    Super::BeginPlay();
    if (IsA<AIslandRescueRaft>()) IslandSceneryVoxels::Register(*this);
    OnRep_State();
}

void AIslandBreakable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AIslandBreakable, State);
}

bool AIslandBreakable::Break(const FVector& Direction)
{
    if (!HasAuthority() || State.bBroken)
    {
        return false;
    }
    const FVector Horizontal = Direction.GetSafeNormal2D();
    State.bBroken = true;
    State.ServerTime = ServerNow();
    State.Direction = Horizontal.IsNearlyZero() ? FVector::ForwardVector : Horizontal;
    OnRep_State();
    ForceNetUpdate();
    return true;
}

double AIslandBreakable::ServerNow() const
{
    const AGameStateBase* GameState = GetWorld()->GetGameState();
    return GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void AIslandBreakable::OnRep_State()
{
    if (!State.bBroken || bVisualStarted)
    {
        return;
    }
    bVisualStarted = true;
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Crown->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (Sign)
    {
        Sign->SetVisibility(false);
    }
    const double Duration = Effect == EIslandBreakEffect::TreeFall ? FallSeconds : FragmentSeconds;
    if (!FApp::CanEverRender() || ServerNow() - State.ServerTime >= Duration)
    {
        HideIntactMesh();
        return;
    }
    if (Effect == EIslandBreakEffect::TreeFall)
    {
        IntactMeshTransform = Mesh->GetComponentTransform();
        Pivot = Mesh->Bounds.Origin - FVector(0, 0, Mesh->Bounds.BoxExtent.Z);
    }
    else
    {
        StartFragments();
        HideIntactMesh();
    }
    SetActorTickEnabled(true);
}

void AIslandBreakable::HideIntactMesh()
{
    Mesh->SetVisibility(false, true);
}

void AIslandBreakable::StartFragments()
{
    UStaticMesh* Piece = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!Piece)
    {
        return;
    }
    const FBoxSphereBounds Bounds = Mesh->Bounds;
    const FVector Scale = (Bounds.BoxExtent / 170.0).ComponentMax(FVector(.07)).ComponentMin(FVector(.65));
    for (int32 Index = 0; Index < 7; ++Index)
    {
        UStaticMeshComponent* Fragment = NewObject<UStaticMeshComponent>(this);
        Fragment->SetStaticMesh(Piece);
        Fragment->SetMaterial(0, Mesh->GetMaterial(0));
        Fragment->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Fragment->SetCanEverAffectNavigation(false);
        Fragment->RegisterComponent();
        Fragment->SetWorldScale3D(Scale);
        Fragments.Add(Fragment);
    }
}

void AIslandBreakable::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const double Age = FMath::Max(0.0, ServerNow() - State.ServerTime);
    if (Effect == EIslandBreakEffect::TreeFall)
    {
        if (Age < FallSeconds)
        {
            const FVector Axis = FVector::CrossProduct(FVector::UpVector, State.Direction).GetSafeNormal();
            const FQuat Fall(Axis, FMath::DegreesToRadians(92.0) * FMath::Square(Age / FallSeconds));
            Mesh->SetWorldLocationAndRotation(Pivot + Fall.RotateVector(IntactMeshTransform.GetLocation() - Pivot),
                Fall * IntactMeshTransform.GetRotation());
            return;
        }
        HideIntactMesh();
    }
    else if (Age < FragmentSeconds)
    {
        for (int32 Index = 0; Index < Fragments.Num(); ++Index)
        {
            const double Angle = Index * UE_TWO_PI / Fragments.Num();
            const FVector Outward(FMath::Cos(Angle), FMath::Sin(Angle), 0);
            const FVector Offset = (Outward * 185.0 + State.Direction * 120.0) * Age
                + FVector(0, 0, 260.0 * Age - 490.0 * Age * Age);
            Fragments[Index]->SetWorldLocationAndRotation(Mesh->Bounds.Origin + Offset,
                FRotator(Index * 28.0 + Age * 340.0, Angle * 45.0 + Age * 200.0, Age * 530.0));
        }
        return;
    }
    for (UStaticMeshComponent* Fragment : Fragments)
    {
        Fragment->DestroyComponent();
    }
    Fragments.Reset();
    SetActorTickEnabled(false);
}

AIslandRescueRaft::AIslandRescueRaft()
{
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Raft(TEXT("/Game/TreasureIsland/Art/Harbour/SM_RescueRaft"));
    GetMesh().SetStaticMesh(Raft.Object);
    GetMesh().SetRelativeScale3D(FVector(2, 2, .35));
}

AIslandSceneryDamage::AIslandSceneryDamage()
{
    bReplicates = true;
    bAlwaysRelevant = true;
}

void AIslandSceneryDamage::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AIslandSceneryDamage, Removed);
}

bool AIslandSceneryDamage::RemoveAuthoredPiece(const FHitResult& Hit)
{
    auto* Mesh = Cast<UStaticMeshComponent>(Hit.GetComponent());
    AActor* Actor = Hit.GetActor();
    if (!Mesh || !Actor || !Actor->HasAuthority() || !Mesh->IsVisible()
        || (!Actor->IsA<AStaticMeshActor>() && !Actor->IsA<AExpeditionLandmark>())) return false;
    UWorld& World = *Actor->GetWorld();
    TActorIterator<AIslandSceneryDamage> It(&World);
    auto* Damage = It ? *It : World.SpawnActor<AIslandSceneryDamage>();
    if (!Damage) return false;
    FIslandRemovedPiece& Loss = Damage->Removed.AddDefaulted_GetRef();
    Loss.Owner = Actor;
    Loss.Component = Mesh->GetFName();
    Damage->OnRep_Removed();
    Damage->ForceNetUpdate();
    Damage->MulticastDebris(Hit.ImpactPoint, Hit.ImpactNormal);
    return true;
}

void AIslandSceneryDamage::OnRep_Removed()
{
    // Reapplying is intentional: actor references can resolve after the initial net bunch.
    for (const auto& Loss : Removed)
    {
        if (!Loss.Owner) continue;
        TInlineComponentArray<UStaticMeshComponent*> Parts(Loss.Owner.Get());
        bool bRemaining = false;
        for (auto* Part : Parts)
        {
            if (Part->GetFName() == Loss.Component)
            {
                Part->SetVisibility(false);
                Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            }
            bRemaining |= Part->GetStaticMesh() && Part->IsVisible();
        }
        if (!bRemaining)
        {
            TInlineComponentArray<UTextRenderComponent*> Signs(Loss.Owner.Get());
            for (auto* Sign : Signs) Sign->SetVisibility(false);
        }
    }
}

void AIslandSceneryDamage::PlayDebris(UWorld& World, FVector Point, FVector Normal, UMaterialInterface* Material,
    UStaticMesh* FragmentShape, FVector FragmentScale, UMaterialInterface* AlternateFinish, bool bLightScraps)
{
    if (World.GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender() || GUsingNullRHI) return;
    const auto* Local = World.GetFirstPlayerController();
    if (Local && Local->PlayerCameraManager &&
        FVector::DistSquared(Local->PlayerCameraManager->GetCameraLocation(), Point) > FMath::Square(4500.)) return;
    UStaticMesh* Shape = FragmentShape ? FragmentShape : LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!Shape) return;
    FActorSpawnParameters Params;
    Params.ObjectFlags |= RF_Transient;
    if (auto* Burst = World.SpawnActor<AIslandChipBurst>(AIslandChipBurst::StaticClass(), Point, FRotator::ZeroRotator, Params))
        Burst->Launch(Normal, *Shape, FragmentScale, Material, AlternateFinish, bLightScraps);
}

void AIslandSceneryDamage::MulticastDebris_Implementation(FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal)
{
    PlayDebris(*GetWorld(),Point,Normal,nullptr);
}
