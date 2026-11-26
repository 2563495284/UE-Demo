#include "Island/IslandClientSettings.h"

#include "AudioDevice.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "ExpeditionText.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Internationalization.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace
{
    bool HasExplicitWindowArguments()
    {
        const TCHAR* Args = FCommandLine::Get();
        FString Size;
        return FParse::Param(Args, TEXT("windowed")) || FParse::Param(Args, TEXT("fullscreen"))
            || FParse::Value(Args, TEXT("Res="), Size) || FParse::Value(Args, TEXT("ResX="), Size)
            || FParse::Value(Args, TEXT("ResY="), Size);
    }

    void ChangeDisplay(EWindowMode::Type Mode, FIntPoint Size)
    {
        const int32 Monitor = GEngine->GameViewport->Viewport->GetCurrentMonitorIndex();
        UGameUserSettings::RequestResolutionChange(Size.X, Size.Y, Mode, false, Monitor);
        IConsoleManager::Get().CallAllConsoleVariableSinks();
    }

    float ClampPreference(float Value, float Minimum, float Maximum)
    {
        return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : 1.f;
    }

    void ApplyMasterVolume(FAudioDeviceHandle Device, float Volume)
    {
        if (Device)
        {
            Device->SetSubmixOutputVolume(&Device->GetMainSubmixObject(), Volume);
        }
    }
}

UIslandClientSettings* UIslandClientSettings::Get()
{
    return GEngine ? Cast<UIslandClientSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UIslandClientSettings::LoadSettings(bool bForceReload)
{
    Super::LoadSettings(bForceReload);
    LookSensitivity = ClampPreference(LookSensitivity, .25f, 3.f);
    MasterVolume = ClampPreference(MasterVolume, 0.f, 1.f);
    Language = Language == TEXT("en") ? TEXT("en") : TEXT("zh-Hans");

    // The engine copies the live size into user settings on every window resize,
    // even when the size came from a one-off multiplayer or QA launch command.
    if (HasExplicitWindowArguments())
    {
        SavedDisplayForLaunchOverride = FDisplayChoice{GetFullscreenMode(), GetScreenResolution()};
    }
#if WITH_EDITOR
    if (GIsEditor)
    {
        ExpeditionText::SetEditorPreviewLanguage(Language == TEXT("en"));
        // Editor startup loads this object before PIE has a world or audio device.
        if (!FWorldDelegates::OnWorldInitializedActors.IsBoundToObject(this))
        {
            FWorldDelegates::OnWorldInitializedActors.AddWeakLambda(this, [this](const FActorsInitializedParams& Params)
            {
                if (Params.World && Params.World->WorldType == EWorldType::PIE)
                {
                    ApplyMasterVolume(Params.World->GetAudioDevice(), MasterVolume);
                }
            });
        }
    }
#endif
}

void UIslandClientSettings::ApplyNonResolutionSettings()
{
    Super::ApplyNonResolutionSettings();
    if (IsRunningDedicatedServer() || IsRunningCommandlet()) return;

#if WITH_EDITOR
    if (GIsEditor)
    {
        ExpeditionText::SetEditorPreviewLanguage(Language == TEXT("en"));
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.WorldType == EWorldType::PIE && Context.World())
            {
                ApplyMasterVolume(Context.World()->GetAudioDevice(), MasterVolume);
            }
        }
        return;
    }
#endif
    FInternationalization::Get().SetCurrentCulture(Language);
    ApplyMasterVolume(GEngine->GetMainAudioDevice(), MasterVolume);
}

void UIslandClientSettings::SaveSettings()
{
    // UGameEngine also saves on exit. Never serialize a pending preview, including
    // the screen size automatically confirmed by its viewport resize callback.
    if (BeforePreview.IsSet()) return;

    if (SavedDisplayForLaunchOverride.IsSet())
    {
        TGuardValue<uint32> Width(ResolutionSizeX, SavedDisplayForLaunchOverride->Size.X);
        TGuardValue<uint32> Height(ResolutionSizeY, SavedDisplayForLaunchOverride->Size.Y);
        TGuardValue<uint32> LastWidth(LastUserConfirmedResolutionSizeX, ResolutionSizeX);
        TGuardValue<uint32> LastHeight(LastUserConfirmedResolutionSizeY, ResolutionSizeY);
        TGuardValue<int32> Mode(FullscreenMode, static_cast<int32>(SavedDisplayForLaunchOverride->Mode));
        TGuardValue<int32> LastMode(LastConfirmedFullscreenMode, FullscreenMode);
        Super::SaveSettings();
        return;
    }
    Super::SaveSettings();
}

bool UIslandClientSettings::CanChangeDisplay() const
{
    return !GIsEditor && FPlatformProperties::SupportsWindowedMode() && GEngine && GEngine->GameViewport
        && GEngine->GameViewport->Viewport && GEngine->GameViewport->GetWindow().IsValid();
}

UIslandClientSettings::FDisplayChoice UIslandClientSettings::LiveDisplay() const
{
    FDisplayChoice Choice{GetFullscreenMode(), GetScreenResolution()};
    if (CanChangeDisplay())
    {
        const auto Window = GEngine->GameViewport->GetWindow();
        Choice.Mode = Window->GetWindowMode();
        if (Choice.Mode != EWindowMode::WindowedFullscreen)
        {
            // Match FSceneViewport::ResizeFrame's client-area units, independent of
            // backbuffer resolution and Retina / Windows display scaling.
            const FVector2D Extent = Window->GetClientSizeInScreen();
            const FIntPoint Size(FMath::RoundToInt(Extent.X), FMath::RoundToInt(Extent.Y));
            if (Size.X > 0 && Size.Y > 0) Choice.Size = Size;
        }
    }
    return Choice;
}

FIslandSettingsDraft UIslandClientSettings::ReadDraft() const
{
    FIslandSettingsDraft Draft;
    const FDisplayChoice Display = LiveDisplay();
    Draft.WindowMode = Display.Mode;
    Draft.Resolution = Display.Size;
    Draft.Quality = GetOverallScalabilityLevel();
    Draft.FrameLimit = FMath::RoundToInt(GetFrameRateLimit());
    Draft.bVSync = IsVSyncEnabled();
    Draft.Language = Language;
    Draft.MasterVolume = MasterVolume;
    Draft.LookSensitivity = LookSensitivity;
    Draft.bInvertY = bInvertY;
    Draft.bCameraShake = bCameraShake;
    return Draft;
}

FIslandSettingsDraft UIslandClientSettings::DefaultDraft() const
{
    FIslandSettingsDraft Draft;
    const TArray<FIntPoint> Sizes = GetWindowResolutions();
    if (!Sizes.IsEmpty() && !Sizes.Contains(Draft.Resolution)) Draft.Resolution = Sizes.Last();
    return Draft;
}

TArray<FIntPoint> UIslandClientSettings::GetWindowResolutions() const
{
    FIntPoint WorkArea = GetDesktopResolution();
    if (FSlateApplication::IsInitialized())
    {
        const auto& Metrics = FSlateApplication::Get().GetCachedDisplayMetricsByRef();
        const int32 Monitor = CanChangeDisplay() ? GEngine->GameViewport->Viewport->GetCurrentMonitorIndex() : INDEX_NONE;
        const FPlatformRect& Rect = Metrics.MonitorInfo.IsValidIndex(Monitor)
            ? Metrics.MonitorInfo[Monitor].WorkArea : Metrics.PrimaryDisplayWorkAreaRect;
        WorkArea = FIntPoint(Rect.Right - Rect.Left, Rect.Bottom - Rect.Top);
    }
    TArray<FIntPoint> Sizes;
    const FIntPoint Common[] = {{960,540}, {1280,720}, {1366,768}, {1440,900}, {1600,900},
                                    {1920,1080}, {1920,1200}, {2560,1440}, {3440,1440}, {3840,2160}};
    for (const FIntPoint& Size : Common)
    {
        // Reserve room for the native title bar.
        if (Size.X <= WorkArea.X && Size.Y + 32 <= WorkArea.Y) Sizes.Add(Size);
    }
    const FDisplayChoice Current = LiveDisplay();
    if (Current.Mode == EWindowMode::Windowed && Current.Size.X > 0 && Current.Size.Y > 0)
    {
        Sizes.AddUnique(Current.Size);
    }
    if (Sizes.IsEmpty()) UKismetSystemLibrary::GetConvenientWindowedResolutions(Sizes);
    Sizes.Sort([](const FIntPoint& A, const FIntPoint& B){ return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
    return Sizes;
}

TArray<FIntPoint> UIslandClientSettings::GetFullscreenResolutions() const
{
    TArray<FIntPoint> Sizes;
    UKismetSystemLibrary::GetSupportedFullscreenResolutions(Sizes);
    if (Sizes.IsEmpty()) Sizes.Add(GetDesktopResolution());
    return Sizes;
}

bool UIslandClientSettings::BeginDisplayPreview(const FIslandSettingsDraft& Draft)
{
    if (!CanChangeDisplay()) return false;
    CancelDisplayPreview();
    const FDisplayChoice Current = LiveDisplay();
    if (Draft.WindowMode == Current.Mode &&
        (Draft.WindowMode == EWindowMode::WindowedFullscreen || Draft.Resolution == Current.Size)) return false;

    BeforePreview = Current;
    ChangeDisplay(Draft.WindowMode, Draft.Resolution);
    return true;
}

void UIslandClientSettings::CancelDisplayPreview()
{
    if (!BeforePreview.IsSet()) return;
    const FDisplayChoice Original = BeforePreview.GetValue();
    if (CanChangeDisplay()) ChangeDisplay(Original.Mode, Original.Size);
    SetFullscreenMode(Original.Mode);
    SetScreenResolution(Original.Size);
    ConfirmVideoMode();
    BeforePreview.Reset();
}

void UIslandClientSettings::CommitDraft(const FIslandSettingsDraft& Draft)
{
    if (BeforePreview.IsSet())
    {
        SetFullscreenMode(Draft.WindowMode);
        SetScreenResolution(Draft.Resolution);
        ConfirmVideoMode();
        SavedDisplayForLaunchOverride.Reset();
        BeforePreview.Reset();
    }
    if (Draft.Quality >= 0 && Draft.Quality != GetOverallScalabilityLevel())
    {
        SetOverallScalabilityLevel(FMath::Clamp(Draft.Quality, 0, 4));
    }
    SetFrameRateLimit(FMath::Clamp(Draft.FrameLimit, 0, 360));
    SetVSyncEnabled(Draft.bVSync);
    Language = Draft.Language == TEXT("en") ? TEXT("en") : TEXT("zh-Hans");
    MasterVolume = ClampPreference(Draft.MasterVolume, 0.f, 1.f);
    LookSensitivity = ClampPreference(Draft.LookSensitivity, .25f, 3.f);
    bInvertY = Draft.bInvertY;
    bCameraShake = Draft.bCameraShake;
    ApplyNonResolutionSettings();
    SaveSettings();
}
