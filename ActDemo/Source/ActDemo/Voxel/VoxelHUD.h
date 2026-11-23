#pragma once

#include "GameFramework/HUD.h"
#include "VoxelHUD.generated.h"

UCLASS()
class AVoxelHUD : public AHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;

protected:
    virtual FString ControlsText() const;
};
