#include "Tests/IslandViewportRecording.h"

#if !UE_BUILD_SHIPPING
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Rendering/SlateRenderer.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "Slate/SlateViewportProvider.h"
#include "UnrealClient.h"

namespace
{
    FDelegateHandle DepartureWindowCreated;
    FDelegateHandle DeparturePresent;
    FDelegateHandle DepartureExit;
    FTSTicker::FDelegateHandle DepartureTick;
    FString DepartureFallback;
    TAtomic<bool> bSawPresent{false};
    TArray<TFuture<bool>> DepartureWrites;
    int32 DepartureSaved = 0;
    int32 DepartureCaptured = 0;

    void DrainDepartureWrite()
    {
        DepartureSaved += DepartureWrites[0].Get() ? 1 : 0;
        DepartureWrites.RemoveAt(0, EAllowShrinking::No);
    }

    void AttachDepartureRecorder(UGameInstance& Instance, const FString& Directory, const double Origin)
    {
        const auto* Client = Instance.GetGameViewportClient();
        const auto Window = Client ? Client->GetWindow() : nullptr;
        if (!Window || DeparturePresent.IsValid()) return;
        UE_LOG(LogTemp, Display, TEXT("ISLAND_UI_CAPTURE attaching_game_window=%p"), Window.Get());
        // The loading thread and the playing world both present to this same Slate window.
        DeparturePresent = FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddLambda(
            [Target = Window.Get(), Directory, Origin, LastStamp = -1](SWindow& Painted, ISlateViewportProvider& Provider) mutable
            {
                if (&Painted != Target) return;
                auto* BackBuffer = Provider.GetBackBufferResource();
                if (!BackBuffer || FPlatformTime::Seconds() - Origin > 120.) return;
                const FIntPoint Size = BackBuffer->GetDesc().Extent;
                if (Size.GetMin() < 2) return;
                bSawPresent.Store(true);
                const int32 Stamp = FMath::Max(LastStamp + 1, FMath::RoundToInt((FPlatformTime::Seconds() - Origin) * 1000.));
                LastStamp = Stamp;
                TArray<FColor> Full;
                FRHICommandListImmediate::Get().ReadSurfaceData(BackBuffer, FIntRect(FIntPoint::ZeroValue, Size), Full, FReadSurfaceDataFlags());
                if (Full.Num() != Size.X * Size.Y) return;
                const int32 Width = FMath::Min(640, Size.X) & ~1;
                const int32 Height = FMath::Max(2, Width * Size.Y / Size.X) & ~1;
                TArray<FColor> Pixels;
                Pixels.SetNumUninitialized(Width * Height);
                for (int32 Y = 0; Y < Height; ++Y)
                    for (int32 X = 0; X < Width; ++X)
                    {
                        FColor& Pixel = Pixels[Y * Width + X];
                        Pixel = Full[(Y * Size.Y / Height) * Size.X + X * Size.X / Width];
                        Pixel.A = 255;
                    }
                const FString Filename = Directory / FString::Printf(TEXT("%09d.png"), Stamp);
                if (DepartureWrites.Num() >= 8) DrainDepartureWrite();
                ++DepartureCaptured;
                DepartureWrites.Add(Async(EAsyncExecution::ThreadPool, [Pixels = MoveTemp(Pixels), Width, Height, Filename] {
                    TArray64<uint8> PNG;
                    FImageUtils::PNGCompressImageArray(Width, Height, Pixels, PNG);
                    return !PNG.IsEmpty() && FFileHelper::SaveArrayToFile(PNG, *Filename);
                }));
            });
    }
}
#endif

void StartIslandViewportRecording(UGameInstance& Instance)
{
#if !UE_BUILD_SHIPPING
    const bool bDeparture = FParse::Param(FCommandLine::Get(), TEXT("IslandUIDeparture"));
    if (!bDeparture && !FParse::Param(FCommandLine::Get(), TEXT("IslandUIRecord"))) return;
    FString Directory;
    FString Role;
    if (!FParse::Value(FCommandLine::Get(), TEXT("IslandUIOutput="), Directory)
        || !FParse::Value(FCommandLine::Get(), TEXT("IslandUIProbe="), Role)) return;
    const bool bWalkthrough = Role == TEXT("Walkthrough");
    if (Role != TEXT("Host") && Role != TEXT("Guest") && Role != TEXT("Solo") && !bWalkthrough) return;
    Directory = FPaths::ConvertRelativePathToFull(Directory) / (Role + TEXT("_frames"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    if (bDeparture)
    {
        FModuleManager::Get().LoadModule(TEXT("ImageWrapper"));
        const double Origin = FPlatformTime::Seconds();
        DepartureExit = FCoreDelegates::OnEnginePreExit.AddStatic(&FinishIslandDepartureRecording);
        DepartureFallback = Directory + TEXT("_viewport");
        IFileManager::Get().MakeDirectory(*DepartureFallback, true);
        DepartureWindowCreated = UGameViewportClient::OnViewportCreated().AddWeakLambda(&Instance, [&Instance, Directory, Origin] {
            AttachDepartureRecorder(Instance, Directory, Origin);
        });
        AttachDepartureRecorder(Instance, Directory, Origin);
        DepartureTick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(&Instance,
            [&Instance, Directory, Origin](float) {
                if (bSawPresent.Load()) return false;
                const double Elapsed = FPlatformTime::Seconds() - Origin;
                if (Elapsed > 120.) return false;
                AttachDepartureRecorder(Instance, Directory, Origin);
                auto* Client = Instance.GetGameViewportClient();
                if (Client && Client->Viewport && Client->Viewport->GetSizeXY().GetMin() > 0
                    && !FScreenshotRequest::IsScreenshotRequested())
                {
                    const FString Filename = FString::Printf(TEXT("%09d.png"), FMath::RoundToInt(Elapsed * 1000.));
                    FScreenshotRequest::RequestScreenshot(DepartureFallback / Filename, true, false);
                }
                return true;
            }), .025f);
        return;
    }
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(&Instance,
        [&Instance, Directory, bWalkthrough, Origin = FPlatformTime::Seconds()] (float)
        {
            const double Elapsed = FPlatformTime::Seconds() - Origin;
            if (Elapsed > (bWalkthrough ? 1500. : 180.)) return false;
            auto* Viewport = Instance.GetGameViewportClient();
            if (Viewport && Viewport->Viewport && Viewport->Viewport->GetSizeXY().GetMin() > 0
                && !FScreenshotRequest::IsScreenshotRequested())
            {
                const auto Filename = FString::Printf(TEXT("%09d.png"), FMath::RoundToInt(Elapsed * 1000.));
                FScreenshotRequest::RequestScreenshot(Directory / Filename, true, false);
            }
            return true;
        }), bWalkthrough ? .2f : .125f);
#endif
}

void FinishIslandDepartureRecording()
{
#if !UE_BUILD_SHIPPING
    FCoreDelegates::OnEnginePreExit.Remove(DepartureExit);
    FTSTicker::GetCoreTicker().RemoveTicker(DepartureTick);
    UGameViewportClient::OnViewportCreated().Remove(DepartureWindowCreated);
    if (DeparturePresent.IsValid())
    {
        FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(DeparturePresent);
        DeparturePresent.Reset();
        FlushRenderingCommands();
    }
    while (!DepartureWrites.IsEmpty()) DrainDepartureWrite();
    if (!DepartureCaptured && !DepartureFallback.IsEmpty())
    {
        TArray<FString> Files;
        IFileManager::Get().FindFiles(Files, *(DepartureFallback / TEXT("*.png")), true, false);
        UE_LOG(LogTemp, Display, TEXT("ISLAND_UI_DEPARTURE_FRAMES saved=%d source=game_viewport"), Files.Num());
    }
    else if (DepartureSaved != DepartureCaptured)
    {
        UE_LOG(LogTemp, Error, TEXT("ISLAND_UI_FAIL final window captures saved=%d expected=%d"), DepartureSaved, DepartureCaptured);
    }
    else
    {
        UE_LOG(LogTemp, Display, TEXT("ISLAND_UI_DEPARTURE_FRAMES saved=%d source=presented_window"), DepartureSaved);
    }
#endif
}
