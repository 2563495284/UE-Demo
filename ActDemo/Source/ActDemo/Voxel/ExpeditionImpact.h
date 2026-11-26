#pragma once

#include "Camera/PlayerCameraManager.h"
#include "ExpeditionImpact.generated.h"

struct FVoxelEdit;
class USoundWave;
class USoundAttenuation;
class UMaterialInstanceDynamic;
class UGameViewportClient;
class SIslandPassage;

UCLASS()
class AExpeditionImpactCamera : public APlayerCameraManager
{
    GENERATED_BODY()

public:
    AExpeditionImpactCamera();
    static void Play(AActor& Terrain, const FVoxelEdit& Edit);
    static void PlayAt(UWorld& World, FVector Point, float Volume, float Pitch, float Shake);
    void UpdateCamera(float DeltaTime) override;
    bool HasPaintedPassageBlack(uint32 Round, bool bReturning) const;

protected:
    void ApplyCameraModifiers(float DeltaTime, FMinimalViewInfo& View) override;
    void BeginPlay() override;
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    UPROPERTY() TObjectPtr<USoundWave> Chip;
    UPROPERTY() TObjectPtr<USoundAttenuation> Attenuation;
    float ImpactAge = 1.f;
    float ImpactStrength = 0.f;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PassageMaterial;
    TSharedPtr<SIslandPassage> PassageLayer;
    TWeakObjectPtr<UGameViewportClient> PassageViewport;
    static constexpr float ImpactDuration = .14f;
};
