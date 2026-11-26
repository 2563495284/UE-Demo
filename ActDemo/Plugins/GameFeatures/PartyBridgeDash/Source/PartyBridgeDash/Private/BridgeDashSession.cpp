#include "BridgeDashSession.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace BridgeDash
{
    constexpr int32 Near[] = {4,14};
    constexpr int32 Far[] = {9,19};
    constexpr int32 HalfWidth = 8;
    constexpr int32 MaxBridgeHeight = 8;
}

ABridgeDashSession::ABridgeDashSession()
{
    Title = TEXT("断桥接力");
    EnglishTitle = TEXT("Bridge Relay");
    EnglishObjective = TEXT("Build a path over the low posts. Cross from shore to shore on two bridges to win.");
    Objective = TEXT("朝低桩堆土接通两岸。从近岸出发，踩过已接通的桥，再登上对岸；先过两段获胜。");
    Theme = FColor(90,173,250);
    RoundSeconds = 70.f;
}

void ABridgeDashSession::BuildArena()
{
    Box({-3,-8,0}, {4,8,0}, EPartyBlock::Stone);
    Box({9,-8,0}, {14,8,0}, EPartyBlock::Stone);
    Box({19,-8,0}, {23,8,0}, EPartyBlock::Stone);
    for (int32 Gap=0; Gap<2; ++Gap)
    {
        // Sunken footings can be targeted from above. Players can build out from
        // these or lay the middle of the deck from the side of a previous block.
        for (const int32 Y : {-4,4})
            for (int32 Offset=1; Offset<=4; ++Offset)
            {
                const int32 Bottom = Offset%2 ? -1 : -2;
                Box({BridgeDash::Near[Gap]+Offset,Y-1,-3},
                    {BridgeDash::Near[Gap]+Offset,Y,Bottom}, EPartyBlock::Stone);
            }
        for (int32 Y : {-8,0,8})
        {
            Marker({BridgeDash::Near[Gap],Y,1},Theme);
            Marker({BridgeDash::Far[Gap],Y,1},FColor(255,200,53));
        }
    }
    for (int32 Seat=0; Seat<8; ++Seat)
        SetStart(Seat, {0,-7+2*Seat,1});
}

void ABridgeDashSession::FindConnectedDeck(int32 NearBank, int32 FarBank, TSet<FIntVector>& Deck) const
{
    // Survey walkable surfaces every half metre, independently of the metre-scale
    // outline used to author the course. Keep stacked paths separate by elevation.
    constexpr int32 Horiz = 50, Vert = 25;
    const int32 MinX = 2*NearBank+1, MaxX = 2*FarBank+1;
    const int32 MinY = -2*BridgeDash::HalfWidth, MaxY = 2*BridgeDash::HalfWidth+1;
    constexpr int32 MinZ = 2, MaxZ = 4*BridgeDash::MaxBridgeHeight;
    TSet<FIntVector> Walkable;
    TArray<FIntVector> Queue;
    Deck.Reset();
    for (int32 X=MinX; X<=MaxX; ++X)
    {
        for (int32 Y=MinY; Y<=MaxY; ++Y)
        {
            const FVector Column = GetActorLocation()+FVector((X+.5)*Horiz,(Y+.5)*Horiz,0.);
            float Below = TerrainDensityAt(Column+FVector(0,0,MinZ*Vert-3.));
            for (int32 Z=MinZ; Z<=MaxZ; ++Z)
            {
                const FVector Top = Column+FVector(0,0,(Z+1)*Vert-3.);
                const float Above = TerrainDensityAt(Top);
                if (Below<=0.f && Above>0.f)
                {
                    bool bClear = true;
                    for (int32 H=Vert; H<=175 && bClear; H+=Vert)
                        bClear = TerrainDensityAt(Top+FVector(0,0,H))>0.f;
                    if (bClear)
                    {
                        const FIntVector P(X,Y,Z);
                        Walkable.Add(P);
                        if (X==MinX && FMath::Abs(Z*Vert-100)<=Vert)
                        {
                            Queue.Add(P);
                            Deck.Add(P);
                        }
                    }
                }
                Below = Above;
            }
        }
    }
    const FIntVector Steps[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    bool bReachedFarBank = false;
    for (int32 I=0; I<Queue.Num(); ++I)
    {
        const FIntVector P = Queue[I];
        if (P.X==MaxX && FMath::Abs(P.Z*Vert-100)<=Vert) bReachedFarBank = true;
        for (FIntVector Step : Steps)
        {
            // The movement component accepts ordinary steps and a 45-degree soil approach.
            for (int32 Delta=-2; Delta<=2; ++Delta)
            {
                const FIntVector Next = P+Step+FIntVector(0,0,Delta);
                if (!Walkable.Contains(Next) || Deck.Contains(Next)) continue;
                Deck.Add(Next);
                Queue.Add(Next);
            }
        }
    }
    if (!bReachedFarBank) Deck.Reset();
}

void ABridgeDashSession::OnTerrainEdited(int32 Seat, const FBox& WorldBounds, bool bPlaced)
{
    bCheckBridges = true;
}

void ABridgeDashSession::OnArenaStep(float DeltaSeconds)
{
    if (bCheckBridges)
    {
        bCheckBridges = false;
        for (int32 Gap=0; Gap<2; ++Gap)
        {
            const bool bWasConnected = !ConnectedDeck[Gap].IsEmpty();
            FindConnectedDeck(BridgeDash::Near[Gap],BridgeDash::Far[Gap],ConnectedDeck[Gap]);
            const bool bComplete = !ConnectedDeck[Gap].IsEmpty();
            if (bComplete!=bWasConnected)
            {
                if (!bComplete)
                    for (const int32 Seat : PlayingSeats())
                        if (Score(Seat)==Gap) Crossing[Seat] = ECrossing::Waiting;
                for (int32 Y : {-8,0,8})
                    SetBeacon({BridgeDash::Far[Gap],Y,1}, bComplete ? FColor(97,255,154) : FColor(255,200,53));
            }
        }
    }
    for (auto It=GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* Controller = It->Get();
        auto* Pawn = Controller ? Cast<APartyVoxelPawn>(Controller->GetPawn()) : nullptr;
        if (Pawn && Pawn->GetSession()==this) CheckRunner(*Pawn);
    }
}

void ABridgeDashSession::CheckRunner(APartyVoxelPawn& Pawn)
{
    const int32 Seat = Pawn.GetSeat();
    if (Seat<0 || Seat>=UE_ARRAY_COUNT(Crossing)) return;
    const int32 Gap = Score(Seat);
    if (Gap<0 || Gap>=UE_ARRAY_COUNT(ConnectedDeck)) return;

    const int32 Near = BridgeDash::Near[Gap];
    const int32 Far = BridgeDash::Far[Gap];
    const FIntVector Feet = FeetCell(Seat);
    ECrossing& Stage = Crossing[Seat];
    const FVector LocalFoot = Pawn.GetActorLocation()-GetActorLocation()-FVector(0,0,Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    if (LocalFoot.Z<50. || FMath::Abs(Feet.Y)>BridgeDash::HalfWidth || Feet.X<Near-4 || Feet.X>Far+4)
    {
        Stage = ECrossing::Waiting;
        return;
    }
    if (!IsGrounded(Seat)) return; // Keep the last real footing through a jump.

    const double NearEdgeX = (CellCenter({Near,0,0}).X+CellCenter({Near+1,0,0}).X)*0.5;
    const bool bTouchesNearBank = Pawn.GetActorLocation().X-Pawn.GetCapsuleComponent()->GetScaledCapsuleRadius()<=NearEdgeX;
    if (bTouchesNearBank && FMath::Abs(LocalFoot.Z-100.)<45.)
    {
        Stage = ECrossing::NearBank;
        return;
    }
    if (Feet.X>Near && Feet.X<Far && Stage==ECrossing::NearBank)
    {
        const FIntVector Footing(FMath::FloorToInt(LocalFoot.X/50.), FMath::FloorToInt(LocalFoot.Y/50.), FMath::FloorToInt(LocalFoot.Z/25.));
        for (int32 Delta=-1; Delta<=1; ++Delta)
            if (ConnectedDeck[Gap].Contains(Footing+FIntVector(0,0,Delta))) Stage=ECrossing::OnBridge;
        return;
    }
    if (Feet.X<Far || FMath::Abs(LocalFoot.Z-100.)>45.) return;
    if (Stage==ECrossing::OnBridge && !ConnectedDeck[Gap].IsEmpty())
    {
        SetScore(Seat,Gap+1);
        if (Gap==1) Finish(Seat);
    }
    Stage = ECrossing::Waiting;
}
