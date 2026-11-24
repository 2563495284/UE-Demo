#pragma once

#include "Voxel/VoxelHUD.h"
#include "IslandHUD.generated.h"

class AIslandPlayerController;
class ACameraActor;
class SIslandRoomPanel;
class SWidget;
enum class EIslandPanel : uint8;

UCLASS()
class AIslandHUD : public AVoxelHUD
{
    GENERATED_BODY()

public:
    void DrawHUD() override;
    void Tick(float DeltaSeconds) override;
    void ToggleMenu();
    bool IsMenuOpen() const;
    bool IsAcceptingWorldInput() const;

protected:
    void BeginPlay() override;
    void EndPlay(const EEndPlayReason::Type Reason) override;
    FString ControlsText() const override;
private:
    void DrawChart(const AIslandPlayerController& Player, float Alpha);
    void UpdateDockCamera(AIslandPlayerController& Player, bool bRoom);
    void UpdateInput(AIslandPlayerController& Player, bool bMenu);
    void UpdateResultCamera(AIslandPlayerController& Player);
    void DrawHunt(AIslandPlayerController& Player);
    TSharedPtr<SIslandRoomPanel> Panel;
    TSharedPtr<SWidget> ViewportLayer;
    TWeakObjectPtr<ACameraActor> DockCamera, ResultCamera;
    double ResultAnnounced = 0.0;
    int32 CameraDock = INDEX_NONE;
    bool bFront = false;
    bool bRoomCamera = false;
    bool bMenuInput = false;
    bool bHandoffInput = false;
    bool bSetInput = false;
    bool bLegacyProbe = false;
    bool bHadFocus = true;
    uint8 LastPhase = 255;
    double HandoffUntil = 0.0;
    float ChartVisibility = 0.f;
};
