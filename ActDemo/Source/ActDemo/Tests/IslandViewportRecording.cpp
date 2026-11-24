#include "Tests/IslandViewportRecording.h"

#if !UE_BUILD_SHIPPING
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#endif

void StartIslandViewportRecording(UGameInstance& Instance)
{
#if !UE_BUILD_SHIPPING
    if (!FParse::Param(FCommandLine::Get(), TEXT("IslandUIRecord"))) return;
    FString Directory;
    FString Role;
    if (!FParse::Value(FCommandLine::Get(), TEXT("IslandUIOutput="), Directory)
        || !FParse::Value(FCommandLine::Get(), TEXT("IslandUIProbe="), Role)) return;
    const bool bWalkthrough = Role == TEXT("Walkthrough");
    if (Role != TEXT("Host") && Role != TEXT("Guest") && Role != TEXT("Solo") && !bWalkthrough) return;
    Directory = FPaths::ConvertRelativePathToFull(Directory) / (Role + TEXT("_frames"));
    IFileManager::Get().MakeDirectory(*Directory, true);
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
