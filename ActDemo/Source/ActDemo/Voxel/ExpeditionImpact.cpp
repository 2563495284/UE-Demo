#include "Voxel/ExpeditionImpact.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"
#include "UObject/ConstructorHelpers.h"
#include "Voxel/VoxelDensity.h"

AExpeditionImpactCamera::AExpeditionImpactCamera()
{
    static ConstructorHelpers::FObjectFinder<USoundWave> Hit(TEXT("/Game/ActDemo/Audio/RockChip"));
    Chip = Hit.Object;
    Attenuation = CreateDefaultSubobject<USoundAttenuation>(TEXT("RockAttenuation"));
    Attenuation->Attenuation.AttenuationShapeExtents.X = 300.f;
    Attenuation->Attenuation.FalloffDistance = 1600.f;
}

void AExpeditionImpactCamera::Play(AActor& Terrain, const FVoxelEdit& Edit)
{
    if (Edit.Action == EVoxelAction::Fill) return;
    auto* Player = Terrain.GetWorld()->GetFirstPlayerController();
    auto* Camera = Player && Player->IsLocalController() ? Cast<AExpeditionImpactCamera>(Player->PlayerCameraManager) : nullptr;
    if (!Camera || !Player->GetPawn()) return;
    const FVector Point(Edit.Center);
    const double Distance = FVector::Distance(Player->GetPawn()->GetActorLocation(), Point);
    if (Distance > 2300.) return;
    UGameplayStatics::PlaySoundAtLocation(Camera, Camera->Chip, Point, .62f, FMath::FRandRange(.93f,1.08f), 0.f, Camera->Attenuation);

    // A short local impulse follows the actual collision commit. Distant drilling is audible only.
    constexpr float Range = 310.f;
    const float Strength = .34f * FMath::Clamp(1.f - float(Distance) / Range, 0.f, 1.f);
    const float Remaining = FMath::Max(0.f, 1.f - Camera->ImpactAge / ImpactDuration);
    if (Strength > Camera->ImpactStrength * Remaining)
    {
        Camera->ImpactAge = 0.f;
        Camera->ImpactStrength = Strength;
    }
}

void AExpeditionImpactCamera::ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& View)
{
    Super::ApplyCameraModifiers(DeltaTime, View);
    if (ImpactAge >= ImpactDuration) return;
    ImpactAge += DeltaTime;
    const float Envelope = FMath::Square(FMath::Max(0.f, 1.f - ImpactAge / ImpactDuration));
    const float Impulse = Envelope * ImpactStrength * FMath::Sin(ImpactAge * 76.f);
    View.Location.Z += Impulse * 3.5f;
    View.Rotation.Pitch += Impulse * .65f;
}
