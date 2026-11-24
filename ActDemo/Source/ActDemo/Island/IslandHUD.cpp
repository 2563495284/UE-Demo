#include "Island/IslandHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "PartyMinigame.h"
#include "Voxel/VoxelCharacter.h"

namespace
{
    const FLinearColor Panel(.023f, .037f, .047f, .96f);
    const FLinearColor Ink(.91f, .94f, .92f);
}

FString AIslandHUD::ControlsText() const
{
    return TEXT("LMB  Punch    RMB  Dig    Q  Fill    Shift  Climb    E  Open    Hold Tab  Map    Hold R  Return");
}

void AIslandHUD::DrawHUD()
{
    const AIslandPlayerController* Player = Cast<AIslandPlayerController>(GetOwningPlayerController());
    const AIslandGameState* Round = GetWorld()->GetGameState<AIslandGameState>();
    if (!Canvas || !Player || !Round)
    {
        return;
    }
    const auto Label = [&](const FString& Text, const FLinearColor& Color, float Y)
    {
        float Width, Height;
        UFont* Font = GEngine->GetSmallFont();
        GetTextSize(Text, Width, Height, Font, 1.05f);
        const float X = (Canvas->ClipX - Width) * .5f;
        DrawRect(Panel, X - 14, Y - 6, Width + 28, Height + 12);
        DrawText(Text, Color, X, Y, Font, 1.05f);
    };
    if (Round->Phase == EIslandPhase::PartyReturning)
    {
        Label(TEXT("Returning to the island"), Ink, Canvas->ClipY * .32f);
        return;
    }
    if (const APartyPawn* Pawn = Cast<APartyPawn>(Player->GetPawn()))
    {
        if (Round->Phase == EIslandPhase::PartyPlaying)
        {
            Pawn->DrawPartyHUD(*this, FVector2D(Canvas->ClipX, Canvas->ClipY));
        }
        else
        {
            Label(TEXT("Getting ready"), Ink, Canvas->ClipY * .32f);
        }
        return;
    }
    if (Round->Phase == EIslandPhase::PartyLoading || Round->Phase == EIslandPhase::PartyPlaying || !Player->IsVoxelInputEnabled())
    {
        Label(TEXT("Entering party challenge"), Ink, Canvas->ClipY * .32f);
        return;
    }
    Super::DrawHUD();
    if (Round->Phase == EIslandPhase::Finished)
    {
        Label(FString::Printf(TEXT("%s crew found the prize"), IslandRules::TeamName(Round->Winner)), IslandRules::TeamColor(Round->Winner), 27.0f);
        if (Player->HasAuthority())
        {
            Label(TEXT("Enter  New round"), Ink, 65.0f);
        }
        return;
    }
    if (Round->Phase == EIslandPhase::Waiting)
    {
        Label(FString::Printf(TEXT("Waiting for crews  %u/6"), Round->JoinedCount), Ink, 27.0f);
        return;
    }
    const int32 Team = Player->GetTeam();
    Label(FString::Printf(TEXT("%s crew    %d/4 clues"), IslandRules::TeamName(Team), Player->GetLocalClues().Count()),
        IslandRules::TeamColor(Team), 27.0f);
    if (const FString Water = Player->WaterStatus(); !Water.IsEmpty())
    {
        Label(Water, Ink, Canvas->ClipY * .34f);
    }
    if (Player->IsMapOpen())
    {
        DrawChart(*Player);
    }
}

void AIslandHUD::DrawChart(const AIslandPlayerController& Player)
{
    const float Size = FMath::Min(Canvas->ClipX, Canvas->ClipY) * .65f;
    const FVector2D Center(Canvas->ClipX * .5, Canvas->ClipY * .5);
    const auto ProjectMap = [&](const FVector2D& P) { return Center + FVector2D(P.Y, -P.X) * (Size / 5050.0); };
    const auto Line = [&](const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Width)
    {
        DrawLine(A.X, A.Y, B.X, B.Y, Color, Width);
    };
    const FVector2D Coast[] = {{2300, 950}, {950, 2300}, {-950, 2300}, {-2300, 950},
                               {-2300, -950}, {-950, -2300}, {950, -2300}, {2300, -950}};
    DrawRect(Panel, Center.X - Size * .5f, Center.Y - Size * .5f, Size, Size);
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Coast); ++Index)
    {
        Line(ProjectMap(Coast[Index]), ProjectMap(Coast[(Index + 1) % UE_ARRAY_COUNT(Coast)]), {.31f, .57f, .59f}, 2.f);
    }

    const FIslandClues& Clues = Player.GetLocalClues();
    const FLinearColor Team = IslandRules::TeamColor(Player.GetTeam());
    if (Clues.Complete())
    {
        Line(ProjectMap(Clues.Points[0]), ProjectMap(Clues.Points[2]), Team, 2.f);
        Line(ProjectMap(Clues.Points[1]), ProjectMap(Clues.Points[3]), Team, 2.f);
        const FVector2D P = ProjectMap(Clues.Crossing);
        Line(P - FVector2D(7, 7), P + FVector2D(7, 7), Ink, 2.5f);
        Line(P + FVector2D(-7, 7), P + FVector2D(7, -7), Ink, 2.5f);
    }
    for (int32 Index = 0; Index < IslandRules::ClueCount; ++Index)
    {
        if ((Clues.Mask & (1u << Index)) != 0)
        {
            const FVector2D P = ProjectMap(Clues.Points[Index]);
            DrawRect(Team, P.X - 3, P.Y - 3, 6, 6);
            DrawText(FString::Printf(TEXT("%c"), 'A' + Index), Ink, P.X + 8, P.Y - 7, GEngine->GetSmallFont(), 1.f);
        }
        else
        {
            const FVector2D P = ProjectMap(IslandRules::SearchCenter(Index));
            const double Radius = IslandRules::SearchRadius * Size / 5050.0;
            for (int32 Segment = 0; Segment < 24; ++Segment)
            {
                const auto Ring = [&](int32 Step)
                {
                    const double Angle = 2.0 * UE_PI * Step / 24.0;
                    return P + Radius * FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
                };
                Line(Ring(Segment), Ring(Segment + 1), {.43f, .62f, .61f}, 1.f);
            }
        }
    }
    if (const AVoxelCharacter* Hero = Player.GetHero())
    {
        const FVector2D P = ProjectMap(FVector2D(Hero->GetActorLocation()));
        DrawRect(Ink, P.X - 2, P.Y - 2, 4, 4);
    }
}
