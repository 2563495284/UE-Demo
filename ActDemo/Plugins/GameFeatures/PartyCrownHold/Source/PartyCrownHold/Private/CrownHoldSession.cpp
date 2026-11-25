#include "CrownHoldSession.h"

namespace CrownHold
{
    const FIntVector Deck[] = {{1,-7,3},{7,0,4},{1,7,3}};
    const FColor Dark(89,108,140);
    const FColor Lit(255,224,54);
}

ACrownHoldSession::ACrownHoldSession()
{
    Title = TEXT("潮滩争旗");
    EnglishTitle = TEXT("Capture the Flag");
    EnglishObjective = TEXT("Build up to the gold flag. Hold it alone for 1 point a second. First to 12 wins.");
    Objective = TEXT("搭阶登上亮金旗台，独占每秒得 1 分。有人同台就暂停计分，先到 12 分获胜。");
    Theme = FColor(176,116,238);
    RoundSeconds = 60.f;
}

void ACrownHoldSession::BuildArena()
{
    Box({-9,-11,0}, {10,11,0}, EPartyBlock::Stone);
    for (const FIntVector& Deck : CrownHold::Deck)
    {
        Box(Deck-FIntVector(1,1,0),Deck+FIntVector(1,1,0),EPartyBlock::Stone);
        Box({Deck.X,Deck.Y,1},Deck,EPartyBlock::Stone);
        Marker(Deck+FIntVector(0,0,3),CrownHold::Dark);
    }
    for (int32 Seat=0; Seat<8; ++Seat)
        SetStart(Seat, {-6,-7+2*Seat,1});
}

void ACrownHoldSession::OnArenaStep(float DeltaSeconds)
{
    const int32 Rotation = FMath::FloorToInt(FMath::Max(0.,double(RoundSeconds)-double(SecondsLeft()))/14.);
    const int32 Next = (int32(uint32(ArenaSeed())%3u)+Rotation)%3;
    if (Next!=LitCrown)
    {
        if (LitCrown!=INDEX_NONE) SetBeacon(CrownHold::Deck[LitCrown]+FIntVector(0,0,3),CrownHold::Dark);
        LitCrown = Next;
        SetBeacon(CrownHold::Deck[Next]+FIntVector(0,0,3),CrownHold::Lit);
    }
    const FIntVector Deck = CrownHold::Deck[LitCrown];
    int32 Occupant = INDEX_NONE;
    for (const int32 Seat : PlayingSeats())
    {
        if (Seat<0 || Seat>=8 || !IsGrounded(Seat)) continue;
        const FIntVector Feet = FeetCell(Seat);
        if (FMath::Abs(Feet.X-Deck.X)>1 || FMath::Abs(Feet.Y-Deck.Y)>1
            || FMath::Abs(FeetLocation(Seat).Z-CellCenter(Deck).Z-50.)>45.) continue;
        if (Occupant!=INDEX_NONE) return;
        Occupant = Seat;
    }
    if (Occupant==INDEX_NONE) return;
    HoldTime[Occupant] += DeltaSeconds;
    const int32 Points = FMath::Min(12,FMath::FloorToInt(HoldTime[Occupant]));
    if (Points!=Score(Occupant)) SetScore(Occupant,Points);
    if (Points==12) Finish(Occupant);
}
