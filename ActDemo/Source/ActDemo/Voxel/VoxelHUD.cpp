#include "Voxel/VoxelHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Voxel/VoxelPlayerController.h"

void AVoxelHUD::DrawHUD()
{
    Super::DrawHUD();
    const AVoxelPlayerController* Player = Cast<AVoxelPlayerController>(GetOwningPlayerController());
    if (!Canvas || !Player)
    {
        return;
    }
    const float X = Canvas->ClipX * .5f;
    const float Y = Canvas->ClipY * .5f;
    const float Scale = FMath::Clamp(Canvas->ClipX / 1120.f, .7f, 1.4f);
    UFont* Font = GEngine->GetSmallFont();
    const auto Centered = [&](const FString& Text, float Baseline)
    {
        float Width, Height;
        GetTextSize(Text, Width, Height, Font, Scale);
        DrawRect(FLinearColor(0.014f, .017f, .022f, .78f), X - Width * .5f - 12, Baseline - 6, Width + 24, Height + 12);
        DrawText(Text, FLinearColor::White, X - Width * .5f, Baseline, Font, Scale);
    };

    Centered(ControlsText(), Canvas->ClipY - 38 * Scale);
    if (Player->IsNoticeVisible())
    {
        Centered(Player->GetNotice(), Y + 33 * Scale);
    }
    if (Player->IsPlaying())
    {
        DrawRect(FLinearColor(0.025f, .025f, .03f, .85f), X - 4, Y - 4, 8, 8);
        DrawRect(FLinearColor::White, X - 1.5f, Y - 1.5f, 3, 3);
    }
    if (const double Progress = Player->GetRecoverProgress(); Progress > 0.0)
    {
        Centered(TEXT("Returning to start"), Y + 63 * Scale);
        DrawRect(FLinearColor(0.03f, .035f, .04f, .9f), X - 53 * Scale, Y + 87 * Scale, 106 * Scale, 4 * Scale);
        DrawRect(FLinearColor(.87f, .92f, .97f), X - 53 * Scale, Y + 87 * Scale, 106 * Scale * Progress, 4 * Scale);
    }
}

FString AVoxelHUD::ControlsText() const
{
    return TEXT("WASD  Move    Space  Jump    LMB  Punch    RMB  Dig    Shift  Climb    Q  Fill    Hold R  Return");
}
