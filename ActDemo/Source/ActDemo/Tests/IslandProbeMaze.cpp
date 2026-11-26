#include "Tests/IslandProbe.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Island/IslandGameMode.h"
#include "Island/IslandPlayerController.h"
#include "PartyVoxelArena.h"

namespace
{
bool HasMazeWall(const APartyVoxelSession& Arena, FVector Midpoint, FVector Normal)
{
    FHitResult Hit;
    const FVector From = Midpoint - Normal * 45.;
    return Arena.GetWorld()->LineTraceSingleByChannel(Hit, From, Midpoint + Normal * 45., ECC_Visibility)
        && Hit.GetComponent() && Hit.GetComponent()->ComponentHasTag(TEXT("MazeWall"));
}

// Read the actual collisions above player height so positions of moving pawns cannot affect the layout check.
bool InspectLayout(const APartyVoxelSession& Arena, uint32& Digest)
{
    constexpr int32 Width = 9;
    const FVector FirstCenter = Arena.GetActorLocation() + FVector(-2350., -2350., 300.);
    const FVector Axes[] = {FVector::ForwardVector, FVector::RightVector};
    TArray<int32> Links[Width * Width];
    Digest = 0;
    for (int32 Y = 0; Y < Width; ++Y)
        for (int32 X = 0; X < Width; ++X)
        {
            const int32 Cell = X + Y * Width;
            for (int32 Axis = 0; Axis < 2; ++Axis)
            {
                if ((Axis == 0 ? X : Y) == Width - 1) continue;
                const int32 Next = Cell + (Axis == 0 ? 1 : Width);
                const FVector Midpoint = FirstCenter + FVector(X * 600., Y * 600., 0.) + Axes[Axis] * 300.;
                const bool bWall = HasMazeWall(Arena, Midpoint, Axes[Axis]);
                Digest = HashCombineFast(Digest, uint32(bWall));
                if (!bWall) { Links[Cell].Add(Next); Links[Next].Add(Cell); }
            }
        }
    TArray<int32> Reachable = {0};
    for (int32 I = 0; I < Reachable.Num(); ++I)
        for (const int32 Next : Links[Reachable[I]]) Reachable.AddUnique(Next);
    return Reachable.Num() == Width * Width;
}
}

void AIslandProbePeer::ClientCheckMaze_Implementation(int32 Round, int32 Stage, FVector Midpoint, FVector Normal)
{
    const auto* PC = Cast<AIslandPlayerController>(GetOwner());
    const auto* Pawn = PC ? Cast<APartyVoxelPawn>(PC->GetPawn()) : nullptr;
    const auto* Game = Pawn ? Pawn->GetSession() : nullptr;
    if (!Game || !Game->IsChaseGame() || Game->GetPhase() != EPartyVoxelPhase::Playing) return;
    if (Stage == 6)
    {
        if (Game->GetRemainingRunners() != 1) return;
        if (Game->IsCaught(Pawn->GetSeat()))
        {
            const auto* Watching = Cast<APartyVoxelPawn>(PC->GetViewTarget());
            if (!Watching || Watching->GetSession() != Game || Watching == Pawn || !Game->IsRunner(Watching->GetSeat())
                || Game->IsCaught(Watching->GetSeat()) || Pawn->GetCharacterMovement()->MovementMode != MOVE_None) return;
            UE_LOG(LogTemp, Display, TEXT("MAZE_QA_SPECTATE round=%d caught=%d watching=%d frozen=1"),
                Round, Pawn->GetSeat(), Watching->GetSeat());
        }
    }
    else if (HasMazeWall(*Game, Midpoint, Normal) != (Stage != 3)) return;
    if (Stage == 1)
    {
        uint32 Digest;
        if (!InspectLayout(*Game, Digest)) return;
        UE_LOG(LogTemp, Display, TEXT("MAZE_QA_LAYOUT round=%d seed=%d hash=%08x reachable=81"),
            Round, Game->ArenaSeed(), Digest);
    }
    UE_LOG(LogTemp, Display, TEXT("MAZE_QA_VIEW round=%d stage=%d seat=%d"), Round, Stage, Pawn->GetSeat());
    ServerCheckedMaze(Round, Stage);
}

void AIslandProbePeer::ServerCheckedMaze_Implementation(int32 Round, int32 Stage)
{
    MazeObservedRound = Round;
    MazeObservedStage = Stage;
}

void AIslandProbe::PlayMazeChase(APartyVoxelSession& Arena)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("ExpeditionMazeQA")))
    {
        // In a mixed deck this checks ordinary survival and reward settlement.
        GameAction = 1;
        return;
    }
    if (Arena.GetHeadStartRemaining() > 0.f) return;
    const auto& Players = Mode->GetPlayers();
    TArray<APartyVoxelPawn*> Runners;
    APartyVoxelPawn* Hunter = nullptr;
    for (const auto& Controller : Players)
    {
        auto* Pawn = CastChecked<APartyVoxelPawn>(Controller->GetPawn());
        if (Arena.IsRunner(Pawn->GetSeat())) Runners.Add(Pawn);
        else if (!Hunter || Pawn->GetSeat() > Hunter->GetSeat()) Hunter = Pawn;
    }
    if (!Hunter || Runners.Num() != 2) { Require(false, TEXT("two runners and two hunting crews entered the maze")); return; }
    APartyVoxelPawn& First = *Runners[0];
    auto* Peer = Peers[Hunter->GetSeat()].Get();
    const auto Observe = [&](int32 Stage)
    {
        bool bReady = true;
        for (const auto& Observer : Peers)
            if (Observer->MazeObservedRound != GameCount + 1 || Observer->MazeObservedStage != Stage)
            {
                Observer->ClientCheckMaze(GameCount + 1, Stage, MazeWallCenter, MazeWallNormal);
                bReady = false;
            }
        return bReady;
    };
    const auto Position = [&](APartyVoxelPawn& Pawn, FVector Offset)
    {
        FVector Location = MazeWallCenter + Offset;
        Location.Z = Arena.GetActorLocation().Z + Pawn.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 3.;
        Pawn.TeleportTo(Location, Pawn.GetActorRotation(), false, true);
        Pawn.GetCharacterMovement()->StopMovementImmediately();
        Pawn.ForceNetUpdate();
    };
    const auto Capture = [&](APartyVoxelPawn& Pawn) { Peer->ClientGameTool(Pawn.GetActorLocation(), true); };
    switch (GameAction)
    {
    case 0:
    {
        TInlineComponentArray<UStaticMeshComponent*> Meshes(&Arena);
        auto* const* Chosen = Meshes.FindByPredicate([](const UStaticMeshComponent* Mesh)
        { return Mesh->ComponentHasTag(TEXT("MazeWall")) && Mesh->IsVisible() && Mesh->IsCollisionEnabled(); });
        if (!Chosen) { Require(false, TEXT("the generated maze has a solid interior wall")); return; }
        MazeWallCenter = (*Chosen)->Bounds.Origin;
        MazeWallCenter.Z = Arena.GetActorLocation().Z + 125.;
        const FVector Extent = (*Chosen)->Bounds.BoxExtent;
        MazeWallNormal = Extent.X < Extent.Y ? FVector::ForwardVector : FVector::RightVector;
        Position(*Hunter, -MazeWallNormal * 88.);
        Position(First, MazeWallNormal * 88.);
        ++GameAction;
        return;
    }
    case 1:
        if (!Observe(GameAction)) return;
        Capture(First);
        MazeBlockedAt = FPlatformTime::Seconds();
        ++GameAction;
        return;
    case 2:
        if (FPlatformTime::Seconds() - MazeBlockedAt < 1.8) return;
        Require(!Arena.IsCaught(First.GetSeat()) && HasMazeWall(Arena,MazeWallCenter,MazeWallNormal),
            TEXT("a close right click into a wall does not catch the runner behind it"));
        Position(First, MazeWallNormal * 110. + FVector::CrossProduct(MazeWallNormal, FVector::UpVector) * 210.);
        Peer->ClientGameTool(MazeWallCenter, false);
        ++GameAction;
        return;
    case 3:
        if (HasMazeWall(Arena,MazeWallCenter,MazeWallNormal)) { Peer->ClientGameTool(MazeWallCenter, false); return; }
        if (!Observe(GameAction)) return;
        Peer->ClientGameTool(MazeWallCenter, true);
        ++GameAction;
        return;
    case 4:
        if (!HasMazeWall(Arena,MazeWallCenter,MazeWallNormal)) { Peer->ClientGameTool(MazeWallCenter, true); return; }
        if (!Observe(GameAction)) return;
        Position(First, -MazeWallNormal * 230.);
        ++GameAction;
        return;
    case 5:
        if (!Arena.IsCaught(First.GetSeat())) { Capture(First); return; }
        Require(Arena.GetRemainingRunners() == 1 && First.GetCharacterMovement()->MovementMode == MOVE_None,
            TEXT("the tagged player is immobilized and counted exactly once"));
        ++GameAction;
        return;
    case 6:
        if (!Observe(GameAction)) return;
        if (GameCount > 0) Position(*Runners[1], -MazeWallNormal * 230.);
        ++GameAction;
        return;
    default:
        // Let the first deadline expire with a live teammate. End the next round by an actual second hit.
        if (GameCount > 0 && !Arena.IsCaught(Runners[1]->GetSeat())) Capture(*Runners[1]);
        return;
    }
}
