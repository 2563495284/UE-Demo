#include "FloodGatesSession.h"

namespace FloodGates
{
    int32 ChannelY(int32 Seat, int32 Channel) { return (Seat-4)*9+1+2*Channel; }
    const FColor Wet(56,185,250);
    const FColor Dry(248,199,69);
}

AFloodGatesSession::AFloodGatesSession()
{
    Title = TEXT("海堤补漏");
    EnglishTitle = TEXT("Floodgates");
    EnglishObjective = TEXT("Plug the four channels. Each blocked channel earns 1 point a second. Rivals can dig through. First to 48 wins.");
    Objective = TEXT("给面前四条水渠填土。堵住的渠每秒得 1 分，先到 48 分获胜。对手能挖开土塞。");
    Theme = FloodGates::Wet;
    RoundSeconds = 50.f;
}

void AFloodGatesSession::BuildArena()
{
    Box({-5,-36,0}, {9,35,0}, EPartyBlock::Stone);
    for (int32 Seat=0; Seat<8; ++Seat)
    {
        const int32 BaseY = (Seat-4)*9;
        const FColor TeamFlag = FLinearColor::MakeFromHSV8(uint8(Seat*31),180,240).ToFColorSRGB();
        Marker({-4,BaseY+4,1},TeamFlag);
        Marker({8,BaseY+4,2},TeamFlag);
        for (int32 Rail=0; Rail<=4; ++Rail)
            Box({1,BaseY+2*Rail,1}, {6,BaseY+2*Rail,1}, EPartyBlock::Stone);
        for (int32 Channel=0; Channel<4; ++Channel)
        {
            const int32 Y = FloodGates::ChannelY(Seat,Channel);
            Marker({0,Y,1},FloodGates::Wet);
            Marker({7,Y,1},FloodGates::Wet);
        }
        SetStart(Seat,{-3,BaseY+4,1});
    }
}

void AFloodGatesSession::OnTerrainEdited(int32 Seat, const FBox& WorldBounds, bool bPlaced)
{
    bCheckWater = true;
}

void AFloodGatesSession::CheckChannels()
{
    bCheckWater = false;
    for (int32 Seat=0; Seat<8; ++Seat)
        for (int32 Channel=0; Channel<4; ++Channel)
        {
            // Follow the water across the channel at its actual waterline. A lump along
            // one edge leaves an opening. Digging a connected opening through a plug restores flow.
            const int32 Y = FloodGates::ChannelY(Seat,Channel);
            constexpr int32 Length = 25, Width = 3;
            bool Open[Length][Width];
            bool Visited[Length][Width] = {};
            TArray<FIntPoint,TInlineAllocator<Length*Width>> Queue;
            for (int32 X=0; X<Length; ++X)
            {
                for (int32 Lane=0; Lane<Width; ++Lane)
                {
                    const FVector Position = CellCenter({1,Y,1}) + FVector(-50.+25.*X,(Lane-1)*30.,-24.);
                    Open[X][Lane] = TerrainDensityAt(Position)>0.;
                    if (X==0 && Open[X][Lane])
                    {
                        Visited[X][Lane] = true;
                        Queue.Emplace(X,Lane);
                    }
                }
            }
            bool bBlocked = true;
            const FIntPoint Neighbors[] = {{1,0},{0,1},{-1,0},{0,-1}};
            for (int32 I=0; I<Queue.Num() && bBlocked; ++I)
            {
                const FIntPoint Current = Queue[I];
                if (Current.X==Length-1) { bBlocked = false; break; }
                for (FIntPoint Step : Neighbors)
                {
                    const FIntPoint Next = Current + Step;
                    if (Next.X<0 || Next.X>=Length || Next.Y<0 || Next.Y>=Width || Visited[Next.X][Next.Y] || !Open[Next.X][Next.Y]) continue;
                    Visited[Next.X][Next.Y] = true;
                    Queue.Add(Next);
                }
            }
            if (bSealed[Seat][Channel]==bBlocked) continue;
            bSealed[Seat][Channel] = bBlocked;
            SetBeacon({7,Y,1}, bBlocked ? FloodGates::Dry : FloodGates::Wet);
        }
}

void AFloodGatesSession::OnArenaStep(float DeltaSeconds)
{
    if (bCheckWater) CheckChannels();
    for (const int32 Seat : PlayingSeats())
    {
        if (Seat<0 || Seat>=8) continue;
        int32 DryCount = 0;
        for (bool bClosed : bSealed[Seat]) DryCount += bClosed ? 1 : 0;
        DryTime[Seat] += DeltaSeconds*DryCount;
        const int32 Points = FMath::Min(48,FMath::FloorToInt(DryTime[Seat]));
        if (Points!=Score(Seat)) SetScore(Seat,Points);
        if (Points>=48) Finish(Seat);
    }
}
