#include "BeaconClimbSession.h"

namespace BeaconClimb
{
    constexpr int32 Decks[] = {-2,4,10};
}

ABeaconClimbSession::ABeaconClimbSession()
{
    Title = TEXT("灯塔登顶");
    EnglishTitle = TEXT("Beacon Climb");
    EnglishObjective = TEXT("Build steps to reach the three lit platforms in order. First to the summit wins.");
    Objective = TEXT("往台边堆土搭阶，依次站上三层亮灯的石台。最高层先到者获胜。");
    Theme = FColor(250,175,42);
    RoundSeconds = 65.f;
}

void ABeaconClimbSession::BuildArena()
{
    Box({-10,-10,0}, {12,10,0}, EPartyBlock::Stone);
    const FColor Lights[] = {{251,167,59},{255,201,86},{255,235,143}};
    for (int32 Level=0; Level<3; ++Level)
    {
        const int32 X = BeaconClimb::Decks[Level];
        const int32 Height = (Level+1)*3;
        Box({X,-8,Height}, {X+1,8,Height}, EPartyBlock::Stone);
        for (const int32 Y : {-8,8})
        {
            Box({X,Y,1}, {X,Y,Height-1}, EPartyBlock::Stone);
            Marker({X,Y,Height+1}, Lights[Level]);
        }
    }
    for (int32 Seat=0; Seat<8; ++Seat)
        SetStart(Seat, {-7,-7+2*Seat,1});
}

void ABeaconClimbSession::OnArenaStep(float DeltaSeconds)
{
    for (const int32 Seat : PlayingSeats())
    {
        const int32 Next = Score(Seat);
        if (Next<0 || Next>=3 || !IsGrounded(Seat)) continue;
        const FIntVector Feet = FeetCell(Seat);
        const int32 X = BeaconClimb::Decks[Next];
        const double Surface = CellCenter({X,Feet.Y,(Next+1)*3}).Z+50.;
        if (Feet.X<X || Feet.X>X+1 || FMath::Abs(Feet.Y)>8 || FMath::Abs(FeetLocation(Seat).Z-Surface)>45.) continue;
        SetScore(Seat,Next+1);
        if (Next==2) Finish(Seat);
    }
}
