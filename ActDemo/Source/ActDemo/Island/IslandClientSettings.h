#pragma once

#include "GameFramework/GameUserSettings.h"
#include "IslandClientSettings.generated.h"

// Edits live in the menu until Apply, including while a display change is being tested.
struct FIslandSettingsDraft
{
    EWindowMode::Type WindowMode = EWindowMode::Windowed;
    FIntPoint Resolution{1280, 720};
    int32 Quality = 3;
    int32 FrameLimit = 60;
    bool bVSync = false;
    FString Language = TEXT("zh-Hans");
    float MasterVolume = 1.f;
    float LookSensitivity = 1.f;
    bool bInvertY = false;
    bool bCameraShake = true;

    bool operator==(const FIslandSettingsDraft& Other) const = default;
};

UCLASS(Config=GameUserSettings)
class UIslandClientSettings : public UGameUserSettings
{
    GENERATED_BODY()

public:
    static UIslandClientSettings* Get();
    FIslandSettingsDraft ReadDraft() const;
    FIslandSettingsDraft DefaultDraft() const;
    TArray<FIntPoint> GetWindowResolutions() const;
    TArray<FIntPoint> GetFullscreenResolutions() const;
    bool CanChangeDisplay() const;

    bool BeginDisplayPreview(const FIslandSettingsDraft& Draft);
    void CancelDisplayPreview();
    void CommitDraft(const FIslandSettingsDraft& Draft);

    float GetLookSensitivity() const { return LookSensitivity; }
    bool IsVerticalLookInverted() const { return bInvertY; }
    bool IsCameraShakeEnabled() const { return bCameraShake; }

    void LoadSettings(bool bForceReload = false) override;
    void ApplyNonResolutionSettings() override;
    void SaveSettings() override;

private:
    UPROPERTY(Config) FString Language = TEXT("zh-Hans");
    UPROPERTY(Config) float MasterVolume = 1.f;
    UPROPERTY(Config) float LookSensitivity = 1.f;
    UPROPERTY(Config) bool bInvertY = false;
    UPROPERTY(Config) bool bCameraShake = true;

    struct FDisplayChoice
    {
        EWindowMode::Type Mode;
        FIntPoint Size;
    };

    FDisplayChoice LiveDisplay() const;
    TOptional<FDisplayChoice> BeforePreview;
    TOptional<FDisplayChoice> SavedDisplayForLaunchOverride;
};
