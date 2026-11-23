#pragma once

#include "GameFramework/Character.h"
#include "Voxel/VoxelDensity.h"
#include "VoxelCharacter.generated.h"

class AVoxelTerrain;
class UAnimSequence;

UCLASS()
class ACTDEMO_API AVoxelCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AVoxelCharacter();

    FVector GetAimOrigin() const;
    void MoveOnGrid(const FVector2D& Input, double ViewYaw);
    void SetWantsClimb(bool bEnabled);
    bool IsClimbingRock() const;
    void PlayTerrainSwing(EVoxelAction Action);
    void ReturnToStart(const FTransform& Start);

protected:
    void Tick(float DeltaSeconds) override;

private:
    UPROPERTY() TObjectPtr<UAnimSequence> SwingAnimation;
    TWeakObjectPtr<AVoxelTerrain> Terrain;
    FVector ClimbDirection = FVector::ForwardVector;
    bool bWantsClimb = false;
};
