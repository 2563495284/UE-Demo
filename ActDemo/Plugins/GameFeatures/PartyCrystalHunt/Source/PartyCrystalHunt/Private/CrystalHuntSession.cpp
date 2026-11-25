#include "CrystalHuntSession.h"

ACrystalHuntSession::ACrystalHuntSession()
{
    Title = TEXT("晶洞寻珠");
    EnglishTitle = TEXT("Crystal Hunt");
    EnglishObjective = TEXT("Break the mud around glowing veins, then strike the crystal. First to four wins.");
    Objective = TEXT("发光矿脉里各埋着一颗金晶。敲开泥壳再敲金晶得分，先取四颗获胜。");
    Theme = FColor(74,205,197);
    RoundSeconds = 50.f;
}

void ACrystalHuntSession::BuildArena()
{
    Box({-12,-11,0}, {12,10,0}, EPartyBlock::Stone);
    FRandomStream Veins(ArenaSeed());
    Pearls.Reset();
    for (int32 X=-6; X<=9; X+=5)
    {
        for (int32 Y=-8; Y<=7; Y+=5)
        {
            // Every crystal has solid cover even when approached from the top or side.
            Box({X-1,Y-1,1}, {X+1,Y+1,4}, EPartyBlock::Clay);
            const FIntVector Crystal(X,Y,Veins.RandRange(1,3));
            Block(Crystal,EPartyBlock::Gold);
            Pearls.Add(Crystal);
            Marker({X,Y,5},Theme);
        }
    }
    for (int32 Seat=0; Seat<8; ++Seat)
        SetStart(Seat, {-10,-7+2*Seat,1});
}

void ACrystalHuntSession::OnTerrainEdited(int32 Seat, const FBox& WorldBounds, bool bPlaced)
{
    if (bPlaced) return;
    // Undercutting the last support can also dislodge a core beyond the scoop.
    for (auto It = Pearls.CreateIterator(); It; ++It)
    {
        const FIntVector Cell = *It;
        if (BlockAt(Cell)==EPartyBlock::Gold) continue;
        It.RemoveCurrent();
        SetBeacon({Cell.X,Cell.Y,5},FColor(58,68,73));
        const int32 Count = Score(Seat)+1;
        SetScore(Seat,Count);
        if (Count>=4) Finish(Seat);
    }
}
