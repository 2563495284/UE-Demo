#include "Island/IslandChipBurst.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "RHIGlobals.h"

AIslandChipBurst::AIslandChipBurst()
{
    PrimaryActorTick.bCanEverTick = true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("HitPoint")));
}

void AIslandChipBurst::Launch(FVector Normal, UStaticMesh& Shape, FVector Scale,
    UMaterialInterface* Finish, UMaterialInterface* Alternate, bool bLightScraps)
{
    Outward = Normal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    Gravity = bLightScraps ? 390. : 870.;
    FVector Tangent, Bitangent;
    Outward.FindBestAxisVectors(Tangent, Bitangent);
    const FVector Lift = FVector::VectorPlaneProject(FVector::UpVector, Outward);
    FRandomStream Random(GetTypeHash(FIntVector(GetActorLocation())));
    constexpr int32 Count = 11;
    for (int32 I = 0; I < Count; ++I)
    {
        const int32 BatchIndex = Alternate && I % 3 == 1 ? 1 : 0;
        auto& Batch = Batches[BatchIndex];
        const double Angle = (double(I) + Random.FRandRange(-.27f,.27f)) * UE_TWO_PI / Count;
        const FVector Fan = Tangent * FMath::Cos(Angle) + Bitangent * FMath::Sin(Angle);
        const bool bLarge = I % 3 == 0;
        const FVector Start = Outward * 9. + Fan * Random.FRandRange(6.f,16.f);
        const FVector Velocity = Outward * Random.FRandRange(155.f,290.f) +
            Fan * Random.FRandRange(140.f,265.f) + Lift * Random.FRandRange(90.f,170.f);
        const double Size = bLarge ? Random.FRandRange(1.45f,1.90f) : Random.FRandRange(.58f,1.05f);
        const FTransform Transform(Random.VRand().Rotation(), Start, Scale * Size);
        Batch.Chips.Add({Start, Velocity, Random.VRand(), Transform, Random.FRandRange(4.f,10.f), Random.FRandRange(.62f,.87f)});
        Batch.Poses.Add(Transform);
    }
    for (int32 B = 0; B < 2; ++B)
    {
        if (Batches[B].Chips.IsEmpty()) continue;
        auto* Mesh = NewObject<UInstancedStaticMeshComponent>(this);
        Chunks[B] = Mesh;
        Mesh->SetupAttachment(RootComponent);
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetStaticMesh(&Shape);
        if (auto* Paint = B == 0 ? Finish : Alternate) Mesh->SetMaterial(0, Paint);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);
        Mesh->SetReceivesDecals(false);
        Mesh->AddInstances(Batches[B].Poses, false, false, false);
        AddInstanceComponent(Mesh);
        Mesh->RegisterComponent();
    }
#if !UE_BUILD_SHIPPING
    bMeasureVisibility = !GUsingNullRHI && FParse::Param(FCommandLine::Get(), TEXT("IslandQA"));
#endif
    SetLifeSpan(.9f);
}

void AIslandChipBurst::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;
    for (int32 B = 0; B < 2; ++B)
    {
        if (!Chunks[B]) continue;
        auto& Batch = Batches[B];
        for (int32 I = 0; I < Batch.Chips.Num(); ++I)
        {
            const auto& Chip = Batch.Chips[I];
            auto& Pose = Batch.Poses[I];
            const FVector Travel = Chip.Velocity * Age - FVector::UpVector * (.5 * Gravity * Age * Age);
            Pose.SetTranslation(Chip.Start + Travel);
            Pose.SetRotation(FQuat(Chip.SpinAxis, Chip.SpinRate * Age) * Chip.Transform.GetRotation());
            Pose.SetScale3D(Chip.Transform.GetScale3D() * FMath::Clamp((Chip.Lifetime - Age) / .18, 0., 1.));
        }
        Chunks[B]->BatchUpdateInstancesTransforms(0, Batch.Poses, false, true, true);
    }
#if !UE_BUILD_SHIPPING
    if (bMeasureVisibility && Age >= .10 && Age < .55 &&
        Chunks[0]->GetLastRenderTimeOnScreen() >= GetWorld()->GetTimeSeconds() - Age)
    {
        FTransform RenderedPose;
        if (Chunks[0]->GetInstanceTransform(0, RenderedPose))
        {
            const double Distance = FVector::DotProduct(RenderedPose.GetTranslation() - Batches[0].Chips[0].Start, Outward);
            if (Distance >= 12.)
            {
                const int32 Count = Chunks[0]->GetInstanceCount() + (Chunks[1] ? Chunks[1]->GetInstanceCount() : 0);
                UE_LOG(LogTemp, Display, TEXT("SCENERY_CHIPS_VISIBLE count=%d outward_cm=%.1f age=%.3f hit=%s"),
                    Count, Distance, Age, *GetActorLocation().ToCompactString());
                bMeasureVisibility = false;
            }
        }
    }
#endif
}
