#include "ReefRunSession.h"

AReefRunSession::AReefRunSession()
{
    Title = TEXT("穿礁竞速");
    EnglishTitle = TEXT("Reef Run");
    EnglishObjective = TEXT("Dig through three reef walls or build over them. Reach the gold line first.");
    Objective = TEXT("挖穿三道珊瑚墙，或堆土翻过去。先踏上尽头的金线获胜。");
    Theme = FColor(243, 111, 87);
    RoundSeconds = 45.f;
}

void AReefRunSession::BuildArena()
{
    Box({-2,-9,0}, {21,9,0}, EPartyBlock::Stone);
    for (const int32 Y : {-9,9})
        Box({-2,Y,1}, {21,Y,3}, EPartyBlock::Stone);
    for (int32 X : {5,10,15})
    {
        Box({X,-8,1}, {X,8,3}, EPartyBlock::Clay);
        for (int32 Y : {-8,0,8}) Marker({X,Y,4}, Theme);
    }
    for (int32 Y=-8; Y<=8; Y+=2)
        Marker({19,Y,1}, FColor(255,220,65));
    for (int32 Seat=0; Seat<8; ++Seat)
        SetStart(Seat, {0,-7+2*Seat,1});
}

void AReefRunSession::OnArenaStep(float DeltaSeconds)
{
    for (const int32 Seat : PlayingSeats())
    {
        if (!IsGrounded(Seat)) continue;
        const FIntVector Feet = FeetCell(Seat);
        if (FMath::Abs(Feet.Y)>8 || Feet.X<0 || Feet.X>21 || Feet.Z<0) continue;
        const int32 Cleared = int32(Feet.X>5) + int32(Feet.X>10) + int32(Feet.X>15);
        if (Cleared>Score(Seat)) SetScore(Seat,Cleared);
        if (Feet.X>=19)
        {
            SetScore(Seat,4);
            Finish(Seat);
        }
    }
}
