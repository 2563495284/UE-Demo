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
    if (!TeamClues.IsValidIndex(Team) || Index < 0 || Index >= HiddenLayout.Num()) return false;
    if (Index == IslandRules::PrizeIndex) return TeamClues[Team].CanOpenPrize();
    if (HiddenLayout.IsDecoy(Index))
    {
        const FName Feature = PartyFeatureForChest(Index);
        return !ConsumedDecoys.Contains(Index) && (!bExpeditionRoom || !Feature.IsNone())
            && !UnavailablePartyFeatures.Contains(Feature);
    }
    if (TeamClues[Team].Mask & (1u << Index)) return false;
    return !bExpeditionRoom || (Exposed[Index] && Exposed[Index]->IsChallengeResolved())
        || !PartyFeatureForChest(Index).IsNone();
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
        if (bExpeditionRoom && Index == IslandRules::PrizeIndex)
        {
            Player.ClientEditRejected(TeamClues[Team].HasKey() ? TEXT("钥匙已合成，还需集齐四个真箱点位")
                : TEXT("需要集齐四个真箱点位和三片碎片合成的钥匙"));
            return;
        }
        Player.ClientEditRejected(
            HiddenLayout.IsDecoy(Index) ? TEXT("此箱的小游戏暂时无法进入") :
            Index == IslandRules::PrizeIndex ? TEXT("Collect all four clues to open the prize") : TEXT("Your crew already has this clue"));
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
            Player.ClientEditRejected(Round->IsIdentityDisguised() ? TEXT("箱锁已被占用") : TEXT("已有两人合撬"));
            return;
        }
        Lock->SetHelper(&Player, Now);
        UE_LOG(LogTemp, Display, TEXT("ISLAND_LOCK assist chest=%d team=%d"), Index, Team);
        return;
    }
    if (Lock->bAssisted || Lock->bSeized)
    {
        Player.ClientEditRejected(Round->IsIdentityDisguised() ? TEXT("箱锁已被占用") :
            Lock->bAssisted ? TEXT("对手正在合撬，箱盖已稳住") : TEXT("对手已经接锁，守住了这次开箱"));
        return;
    }

    if (AIslandPlayerController* Previous = Lock->Leader.Get())
    {
        Previous->ClientEditRejected(Round->IsIdentityDisguised() ? TEXT("箱锁已易手") : TEXT("对手夺走箱锁"));
    }
    UE_LOG(LogTemp, Display, TEXT("ISLAND_LOCK seize chest=%d from=%d to=%d"), Index, Lock->Team, Team);
    Lock->Team = Team;
    Lock->Leader = &Player;
    Lock->bSeized = true;
    Lock->FinishAt = Now + IslandRules::LockSeconds * .5;
}

void AIslandGameMode::UpdateChestLocks(double Now)
{
    // Several locks can expire between two server ticks. Resolve them in gameplay time
    // order so a trap cannot interrupt an earlier treasure win by TMap iteration order.
    struct FFinishedLock { int32 Index; double At; };
    TArray<FFinishedLock, TInlineAllocator<8>> Finished;
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
        const double PreviousFinish = Lock.FinishAt;
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
        if (PreviousFinish <= Now) Lock.FinishAt = PreviousFinish;
        if (Now >= Lock.FinishAt) Finished.Add({Index, Lock.FinishAt});
    }
    Finished.Sort([](const FFinishedLock& A, const FFinishedLock& B)
    {
        if (A.At != B.At) return A.At < B.At;
        const auto Priority = [](int32 Index) { return Index == IslandRules::PrizeIndex ? -1 : Index; };
        return Priority(A.Index) < Priority(B.Index);
    });
    for (const FFinishedLock& Completion : Finished)
    {
        const int32 Index = Completion.Index;
        const int32 Team = ChestLocks.FindChecked(Index).Team;
        ChestLocks.Remove(Index);
        UE_LOG(LogTemp, Display, TEXT("ISLAND_LOCK complete chest=%d team=%d"), Index, Team);
        if (Index == IslandRules::PrizeIndex)
        {
            ChestLocks.Reset();
            PublishWinner(Team);
            const auto* Round = GetGameState<AIslandGameState>();
            if (bExpeditionRoom && Round->Phase == EIslandPhase::Finished && IsValid(Exposed[Index]))
                Exposed[Index]->RevealPrize(Round->GetServerWorldTimeSeconds());
            return;
        }
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
                EIslandLockRole PlayerRole = EIslandLockRole::Observer;
                if (Lock.Leader.Get() == Player) PlayerRole = EIslandLockRole::Leader;
                else if (Lock.Helper.Get() == Player) PlayerRole = EIslandLockRole::Helper;
                const double Distance = Chest->GetInteractionDistanceSquared(Hero.GetAimOrigin());
                if (PlayerRole == EIslandLockRole::Observer && Distance >= Nearest) continue;
                Next.Chest = Chest;
                Next.Team = Lock.Team;
                Next.Role = PlayerRole;
                Next.bAssisted = Lock.bAssisted;
                Next.bSeized = Lock.bSeized;
                Next.bEligible = CanClaimChest(Member->Team, Pair.Key);
                Next.FinishAt = Lock.FinishAt;
                Nearest = Distance;
                if (PlayerRole != EIslandLockRole::Observer) break;
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
