#pragma once

#include "Camera/PlayerCameraManager.h"
#include "ExpeditionImpact.generated.h"

struct FVoxelEdit;
class USoundWave;
class USoundAttenuation;

UCLASS()
class AExpeditionImpactCamera : public APlayerCameraManager
{
    GENERATED_BODY()

public:
    AExpeditionImpactCamera();
    static void Play(AActor& Terrain, const FVoxelEdit& Edit);

protected:
    void ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& View) override;

private:
    UPROPERTY() TObjectPtr<USoundWave> Chip;
    UPROPERTY() TObjectPtr<USoundAttenuation> Attenuation;
    float ImpactAge = 1.f;
    float ImpactStrength = 0.f;
    static constexpr float ImpactDuration = .14f;
};
