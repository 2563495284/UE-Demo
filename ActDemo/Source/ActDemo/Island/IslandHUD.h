#pragma once

#include "Voxel/VoxelHUD.h"
#include "IslandHUD.generated.h"

class AIslandPlayerController;

UCLASS()
class AIslandHUD : public AVoxelHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;

protected:
    FString ControlsText() const override;

private:
    void DrawChart(const AIslandPlayerController& Player);
};
