#include "Island/IslandGameMode.h"

#include "Island/IslandPlayerController.h"
#include "Island/IslandState.h"
#include "Island/IslandWorld.h"
#include "Voxel/VoxelCharacter.h"

void AIslandGameMode::FChestLock::SetHelper(AIslandPlayerController* NewHelper, double Now)
{
    const double RemainingWork = FMath::Max(0.0, FinishAt - Now) * (bAssisted ? 2.0 : 1.0);
    Helper = NewHelper;
    bAssisted = NewHelper != nullptr;
    FinishAt = Now + RemainingWork / (bAssisted ? 2.0 : 1.0);
}

bool AIslandGameMode::CanClaimChest(int32 Team, int32 Index) const
{
    if (!TeamClues.IsValidIndex(Team)) return false;
    if (Index == IslandRules::PrizeIndex) return TeamClues[Team].Complete();
    return Index >= 0 && Index < IslandRules::ClueCount && (TeamClues[Team].Mask & (1u << Index)) == 0;
}

void AIslandGameMode::WorkChest(AIslandPlayerController& Player, int32 Index)
{
    AIslandGameState* Round = GetGameState<AIslandGameState>();
    const double Now = Round->GetServerWorldTimeSeconds();
    const int32 Team = Player.GetTeam();
    const bool bCouldClaimBeforeUpdate = CanClaimChest(Team, Index);
    // Resolve expired timers and anyone who has stepped away before judging this input.
    UpdateChestLocks(Now);
    if (Round->Phase != EIslandPhase::Hunting) return;

    if (!CanClaimChest(Team, Index))
    {
        // This input may have just completed the crew's timed lock.
        if (bCouldClaimBeforeUpdate) return;
        Player.ClientEditRejected(Index == IslandRules::PrizeIndex
            ? TEXT("Collect all four clues to open the prize") : TEXT("Your crew already has this clue"));
        return;
    }

    for (const auto& Pair : ChestLocks)
    {
        if (Pair.Value.Leader.Get() == &Player || Pair.Value.Helper.Get() == &Player)
        {
            if (Pair.Key != Index) Player.ClientEditRejected(TEXT("先离开正在撬的宝箱"));
            return;
        }
    }

    FChestLock* Lock = ChestLocks.Find(Index);
    if (!Lock)
    {
        FChestLock& NewLock = ChestLocks.Add(Index);
        NewLock.Team = Team;
        NewLock.Leader = &Player;
        NewLock.FinishAt = Now + IslandRules::LockSeconds;
        UE_LOG(LogTemp, Display, TEXT("ISLAND_LOCK start chest=%d team=%d"), Index, Team);
        return;
    }
    if (Lock->Team == Team)
    {
        if (Lock->bAssisted)
        {
            Player.ClientEditRejected(TEXT("已有两人合撬"));
            return;
        }
        Lock->SetHelper(&Player, Now);
        UE_LOG(LogTemp, Display, TEXT("ISLAND_LOCK assist chest=%d team=%d"), Index, Team);
        return;
    }
    if (Lock->bAssisted || Lock->bSeized)
    {
        Player.ClientEditRejected(Lock->bAssisted ? TEXT("对手正在合撬，箱盖已稳住") : TEXT("对手已经接锁，守住了这次开箱"));
        return;
    }

    if (AIslandPlayerController* Previous = Lock->Leader.Get())
    {
        Previous->ClientEditRejected(TEXT("对手夺走箱锁"));
    }
    UE_LOG(LogTemp, Display, TEXT("ISLAND_LOCK seize chest=%d from=%d to=%d"), Index, Lock->Team, Team);
    Lock->Team = Team;
    Lock->Leader = &Player;
    Lock->bSeized = true;
    Lock->FinishAt = Now + IslandRules::LockSeconds * .5;
}

void AIslandGameMode::UpdateChestLocks(double Now)
{
    int32 WinningTeam = INDEX_NONE;
    for (auto It = ChestLocks.CreateIterator(); It; ++It)
    {
        const int32 Index = It.Key();
        FChestLock& Lock = It.Value();
        const AIslandChest* Chest = Exposed[Index];
        const auto CanStay = [&](const AIslandPlayerController* Member)
        {
            return Member && CanPlayerAct(*Member) && Chest && Chest->IsReachableFrom(*Member->GetHero());
        };
        const bool bLeaderPresent = CanStay(Lock.Leader.Get());
        const bool bHelperPresent = Lock.bAssisted && CanStay(Lock.Helper.Get());
        if (!bLeaderPresent && !bHelperPresent)
        {
            UE_LOG(LogTemp, Display, TEXT("ISLAND_LOCK abandoned chest=%d team=%d"), Index, Lock.Team);
            It.RemoveCurrent();
            continue;
        }
        if (!bLeaderPresent)
        {
            Lock.Leader = Lock.Helper;
            Lock.SetHelper(nullptr, Now);
            UE_LOG(LogTemp, Display, TEXT("ISLAND_LOCK handoff chest=%d team=%d"), Index, Lock.Team);
        }
        else if (Lock.bAssisted && !bHelperPresent)
        {
            Lock.SetHelper(nullptr, Now);
        }
        if (Now < Lock.FinishAt) continue;

        const int32 Team = Lock.Team;
        UE_LOG(LogTemp, Display, TEXT("ISLAND_LOCK complete chest=%d team=%d"), Index, Team);
        It.RemoveCurrent();
        if (Index == IslandRules::PrizeIndex)
        {
            WinningTeam = Team;
            break;
        }
        GiveClue(Team, Index);
    }
    if (WinningTeam != INDEX_NONE)
    {
        ChestLocks.Reset();
        PublishWinner(WinningTeam);
    }
}

void AIslandGameMode::PublishLockViews()
{
    for (AVoxelPlayerController* Controller : GetPlayers())
    {
        auto* Player = CastChecked<AIslandPlayerController>(Controller);
        auto* Member = Player->GetPlayerState<AIslandPlayerState>();
        if (!Member) continue;

        FIslandLockView Next;
        if (CanPlayerAct(*Player))
        {
            // The same nearby chest used by E also owns its HUD prompt.
            // Prefer ongoing work if two chests happen to be within reach.
            double Nearest = MAX_dbl;
            const AVoxelCharacter& Hero = *Player->GetHero();
            for (const auto& Pair : ChestLocks)
            {
                const FChestLock& Lock = Pair.Value;
                AIslandChest* Chest = Exposed[Pair.Key];
                if (!Chest || !Chest->IsReachableFrom(Hero)) continue;
                EIslandLockRole Role = EIslandLockRole::Observer;
                if (Lock.Leader.Get() == Player) Role = EIslandLockRole::Leader;
                else if (Lock.Helper.Get() == Player) Role = EIslandLockRole::Helper;
                const double Distance = FVector::DistSquared(Hero.GetAimOrigin(), Chest->GetActorLocation());
                if (Role == EIslandLockRole::Observer && Distance >= Nearest) continue;
                Next.Chest = Chest;
                Next.Team = Lock.Team;
                Next.Role = Role;
                Next.bAssisted = Lock.bAssisted;
                Next.bSeized = Lock.bSeized;
                Next.bEligible = CanClaimChest(Member->Team, Pair.Key);
                Next.FinishAt = Lock.FinishAt;
                Nearest = Distance;
                if (Role != EIslandLockRole::Observer) break;
            }
        }
        const FIslandLockView& Old = Member->LockView;
        if (Old.Chest != Next.Chest || Old.Team != Next.Team || Old.Role != Next.Role
            || Old.bAssisted != Next.bAssisted || Old.bSeized != Next.bSeized
            || Old.bEligible != Next.bEligible || Old.FinishAt != Next.FinishAt)
        {
            Member->SetLockView(Next);
        }
    }
}
