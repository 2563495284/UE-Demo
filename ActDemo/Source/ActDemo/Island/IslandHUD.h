#pragma once

#include "Voxel/VoxelHUD.h"
#include "IslandHUD.generated.h"

class AIslandPlayerController;
class AIslandGameState;
struct FIslandClues;
class AIslandChest;
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
    bool GetTreasureFeedbackFrame(FName& OutStage, float& OutAge) const;
#if !UE_BUILD_SHIPPING
    FIntPoint GetDrawnCrewCounts() const { return DrawnCrewCounts; }
#endif

protected:
    void BeginPlay() override;
    void EndPlay(const EEndPlayReason::Type Reason) override;
    FString ControlsText() const override;
private:
    void DrawChart(const AIslandPlayerController& Player, float Alpha, bool bInset = false);
    void UpdateTreasureFeedback(const AIslandPlayerController& Player, const AIslandGameState& Round, float DeltaSeconds);
    void DrawTreasureLedger(const FIslandClues& Clues, int32 Team, float Scale);
    void DrawTreasureFeedback(const AIslandPlayerController& Player, float Scale, bool bFullChart);
    void DrawVaultBearing(const AIslandPlayerController& Player, float Scale);
    void DrawIntelDiamond(FVector2D Center, float Radius, FLinearColor Color, float Thickness);
    void UpdateDockCamera(AIslandPlayerController& Player, bool bRoom);
    void UpdateInput(AIslandPlayerController& Player, bool bMenu);
    void UpdateResultCamera(AIslandPlayerController& Player);
    void DrawHunt(AIslandPlayerController& Player);
    void DrawNearbyCrews(const AIslandPlayerController& Player, float Scale);
    void DrawLockPrompt(const AIslandPlayerController& Player, const AIslandChest& Chest, float Scale);
    int32 TreasureTeam = INDEX_NONE;
    uint8 SeenSites = 0;
    uint8 PresentedSites = 0;
    TArray<int32> PendingSites;
    int32 RewardSite = INDEX_NONE;
    float RewardAge = 0.f;
    int32 SeenDecoys = 0;
    int32 RevealedDecoy = INDEX_NONE;
    float DecoyAge = 0.f;
    uint8 SeenIntel = 0;
    int32 IntelSite = INDEX_NONE;
    float IntelAge = 0.f;
    bool bCanPresentRewards = false;
    TSharedPtr<SIslandRoomPanel> Panel;
    TSharedPtr<SWidget> ViewportLayer;
    TWeakObjectPtr<ACameraActor> DockCamera, ResultCamera;
    TArray<TWeakObjectPtr<AActor>> ResultOccluders;
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
#if !UE_BUILD_SHIPPING
    FIntPoint DrawnCrewCounts = FIntPoint::ZeroValue;
#endif
};
