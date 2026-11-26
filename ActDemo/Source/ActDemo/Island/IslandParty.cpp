#include "Island/IslandGameMode.h"

#include "Algo/AllOf.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameSession.h"
#include "Island/IslandPlayerController.h"
#include "Island/IslandNeutralProp.h"
#include "Abilities/ExpeditionMudEffect.h"
#include "Island/IslandState.h"
#include "Island/IslandWorld.h"
#include "Misc/SecureHash.h"
#include "PartyMinigame.h"
#include "PartyVoxelArena.h"
#include "Voxel/VoxelCharacter.h"
#include "Voxel/VoxelTerrain.h"

namespace
{
constexpr double PartyAssetTimeout = 15.;
constexpr double PartyDisplayTimeout = 45.;
const FName MazeChaseFeature(TEXT("PartyMazeChase"));
}

void AIslandGameMode::PrepareExpeditionParties(int32 Seed)
{
    DecoyEventSeed = Seed;
    // The public arenas share one short derived seed. Replicating their variants
    // never transmits the private island seed used for the buried chest positions.
    const uint32 SeedDomain[] = {0x4152454Eu, uint32(Seed)};
    const FSHAHash Digest = FSHA1::HashBuffer(SeedDomain, sizeof(SeedDomain));
    PartyLayoutSeed = (int32(Digest.Hash[0]) << 8) | Digest.Hash[1];
    ExpeditionPartyDeck = {TEXT("PartyReefRun"), TEXT("PartyBeaconClimb"), TEXT("PartyCrystalHunt"),
        TEXT("PartyBridgeDash"), TEXT("PartyFloodGates"), TEXT("PartyCrownHold")};
    FRandomStream Random(static_cast<int32>(uint32(Seed) ^ 0x37BFC983u));
    for (int32 I = ExpeditionPartyDeck.Num() - 1; I > 0; --I)
        ExpeditionPartyDeck.Swap(I, Random.RandRange(0, I));
    ExpeditionPartyDeck.Insert(MazeChaseFeature, Random.RandRange(0, ExpeditionPartyDeck.Num()));
    ScatterRandom.Initialize(static_cast<int32>(uint32(Seed) ^ 0xB1C98713u));
#if !UE_BUILD_SHIPPING
    FString OnlyGame;
    if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionGamesQA"))
        && FParse::Value(FCommandLine::Get(), TEXT("ExpeditionGameOnly="), OnlyGame))
    {
        const FName Requested(*OnlyGame);
        if (ExpeditionPartyDeck.Contains(Requested)) ExpeditionPartyDeck = {Requested};
    }
    else if (FParse::Param(FCommandLine::Get(), TEXT("ExpeditionScoreGamesQA")))
    {
        ExpeditionPartyDeck.Remove(MazeChaseFeature);
    }
#endif
    UnavailablePartyFeatures.Reset();
    bPartyUnavailable = false;
}

FName AIslandGameMode::PartyFeatureForChest(int32 Index) const
{
    if (!bExpeditionRoom) return DecoyMinigameFeature;
    if (Index < 0 || Index >= HiddenLayout.Num() || Index == IslandRules::PrizeIndex) return NAME_None;
    int32 TeamMask = 0;
    for (const AVoxelPlayerController* Player : GetPlayers())
    {
        const auto* Member = Player->GetPlayerState<AIslandPlayerState>();
        if (Member && TeamClues.IsValidIndex(Member->Team)) TeamMask |= 1 << Member->Team;
    }
    const bool bTwoCrews = FPlatformMath::CountBits(uint32(TeamMask)) >= 2;
    TArray<FName,TInlineAllocator<8>> Eligible;
    for (FName Feature : ExpeditionPartyDeck)
        if ((Feature != MazeChaseFeature || bTwoCrews) && !UnavailablePartyFeatures.Contains(Feature))
            Eligible.Add(Feature);
    return Eligible.IsEmpty() ? NAME_None : Eligible[PartyRoundId % Eligible.Num()];
}

void AIslandGameMode::AwardPartyTreasure(bool bAllowSand)
{
    if (!bExpeditionRoom || !PartySession) return;
    auto* Round = GetGameState<AIslandGameState>();
    if (Round->PartyOutcome != EIslandChestOutcome::Unknown) return;
    const bool bDecoy = HiddenLayout.IsDecoy(PartyChestIndex);
    FPartyFinish Finish;
    const int32 Seat = PartySession->GetWinningSeat();
    for (const auto& Pair : PartyPlayers)
    {
        const auto* Member = Pair.Key.IsValid() ? Pair.Key->GetPlayerState<AIslandPlayerState>() : nullptr;
        if (!Member || !TeamClues.IsValidIndex(Member->Team)) continue;
        ++Finish.Participants;
        Finish.CrewMask |= 1 << Member->Team;
        if (Pair.Value.Seat == Seat)
        {
            Finish.WinnerName = Member->GetPlayerName();
            Finish.WinnerTeam = Member->Team;
        }
    }
    Finish.WinnerMask = PartySession->GetWinningTeamMask();
    if (Finish.WinnerTeam != INDEX_NONE) Finish.WinnerMask |= 1 << Finish.WinnerTeam;

    if (bDecoy)
    {
        // A whole maze crew can disconnect during the results screen. Only crews still here return to the island.
        Finish.WinnerMask &= Finish.CrewMask;
        auto Plan = PlanDecoyWorldEvents();
        if (!bAllowSand) Plan.SandCenters.Reset();
        const auto Event = IslandRules::DrawDecoyEvent(DecoyEventSeed, PartyChestIndex,
            Finish.Participants, Finish.CrewMask, Finish.WinnerMask, Plan.Mask());
        if (Event == EIslandDecoyEvent::SandRamp)
        {
            // Keep the island pawns parked until the single replicated fill has committed.
            // A rejected edit must never be announced as a completed sand ramp.
            bDecoySandPending = true;
            const auto Centers = Plan.SandCenters;
            EnqueueEnvironmentFill(Centers, [WeakMode = TWeakObjectPtr<AIslandGameMode>(this), Id = PartyRoundId,
                Plan = MoveTemp(Plan), Finish = MoveTemp(Finish)](bool bCommitted) mutable
            {
                auto* Mode = WeakMode.Get();
                if (!Mode || Mode->PartyRoundId != Id || !Mode->bDecoySandPending) return;
                Mode->bDecoySandPending = false;
                if (Mode->GetGameState<AIslandGameState>()->Phase != EIslandPhase::PartyPlaying) return;
                if (!bCommitted)
                {
                    Mode->AwardPartyTreasure(false);
                    return;
                }
                Mode->SettleDecoy(EIslandDecoyEvent::SandRamp, Plan, Finish);
                Mode->CompletePartyReturn(true);
            });
            return;
        }
        SettleDecoy(Event, Plan, Finish);
    }
    else
    {
        PublishPartyFinish(Finish);
        Round->PartyOutcome = EIslandChestOutcome::Genuine;
        Exposed[PartyChestIndex]->MarkChallengeResolved();
        // Opening still pays if nobody scored or the winner was the opener.
        for (int32 Team = 0; Team < TeamClues.Num(); ++Team)
        {
            if (Team != PartyOpenerTeam && !Round->IsPartyWinner(Team)) continue;
            if (GiveClue(Team, PartyChestIndex)) Round->PartyRewardMask |= 1 << Team;
        }
    }
    CompletePartyReturn(true);
}

void AIslandGameMode::PublishPartyFinish(const FPartyFinish& Finish)
{
    auto* Round = GetGameState<AIslandGameState>();
    Round->PartySite = HiddenLayout.IsDecoy(PartyChestIndex)
        ? HiddenLayout.Decoys[PartyChestIndex - HiddenLayout.Centers.Num()].ClueIndex : PartyChestIndex;
    Round->PartyOpeningTeam = PartyOpenerTeam;
    Round->PartyWinnerTeam = Finish.WinnerTeam;
    Round->PartyWinnerName = Finish.WinnerName;
    Round->PartyWinnerMask = Finish.WinnerMask;
}

void AIslandGameMode::SettleDecoy(EIslandDecoyEvent Event, const FDecoyWorldPlan& Plan, const FPartyFinish& Finish)
{
    auto* Round = GetGameState<AIslandGameState>();
    if (Round->PartyOutcome != EIslandChestOutcome::Unknown) return;
    PublishPartyFinish(Finish);
    Round->PartyDecoyEvent = Event;
    if (Event == EIslandDecoyEvent::SailGust)
    {
        for (auto Sail : Plan.Sails)
            if (Sail.IsValid()) Sail->ToggleSail();
    }
    Round->PartyOutcome = EIslandChestOutcome::Decoy;
    Round->SpentFalsePoints.Add(FVector2D(HiddenLayout.GetCenter(PartyChestIndex)));
    ConsumedDecoys.Add(PartyChestIndex);
    if (IsValid(Exposed[PartyChestIndex])) Exposed[PartyChestIndex]->Destroy();
    Exposed[PartyChestIndex] = nullptr;
    UE_LOG(LogTemp, Display, TEXT("ISLAND_DECOY_DRAW round=%u chest=%d event=%d players=%d crews=%d winners=%d world_mask=%d"),
        PartyRoundId, PartyChestIndex, int32(Round->PartyDecoyEvent), Finish.Participants, Finish.CrewMask, Finish.WinnerMask, Plan.Mask());
}

void AIslandGameMode::ScatterLosingCrews()
{
    const auto* Round = GetGameState<AIslandGameState>();
    TArray<AIslandPlayerController*> Losers;
    TArray<FVector> KeepAwayFrom;
    for (AVoxelPlayerController* Member : GetPlayers())
    {
        auto* Player = CastChecked<AIslandPlayerController>(Member);
        if (!Player->GetHero() || !PartyPlayers.Contains(Player)) continue;
        KeepAwayFrom.Add(Player->GetHero()->GetActorLocation());
        if (!Round->IsPartyWinner(Player->GetTeam())) Losers.Add(Player);
    }
    if (Losers.IsEmpty()) return;

    // Different angular sectors avoid clustering even when all participants belong to one losing crew.
    for (int32 I = Losers.Num() - 1; I > 0; --I) Losers.Swap(I, ScatterRandom.RandRange(0, I));
    const double Rotation = ScatterRandom.FRand() * UE_TWO_PI;
    for (int32 Index = 0; Index < Losers.Num(); ++Index)
    {
        auto& Player = *Losers[Index];
        auto* Hero = Player.GetHero();
        const double Angle = Rotation + UE_TWO_PI * Index / Losers.Num();
        const double Radius = GetTerrain()->GetShoreExtent() * ScatterRandom.FRandRange(.38f, .68f);
        const FVector Hint(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.);
        const FTransform Target = FindSafeReturn(Player, Hint, KeepAwayFrom);
        KeepAwayFrom.Add(Target.GetLocation());
        BurialSites.Remove(&Player);
        UExpeditionMudMarkEffect::Clear(*Hero);
        Hero->ReturnToStart(Target);
        Player.ClientResetView(Target.Rotator());
    }
}

void AIslandGameMode::BeginParty(int32 ChestIndex, int32 OpeningTeam)
{
    AIslandGameState* Round = GetGameState<AIslandGameState>();
    Round->PartyStartedAt = Round->GetServerWorldTimeSeconds();
    Round->PartyWinnerName.Reset();
    Round->PartyWinnerTeam = INDEX_NONE;
    Round->PartyWinnerMask = 0;
    Round->PartyWinnerUntil = 0.;
    Round->PartyOutcome = EIslandChestOutcome::Unknown;
    Round->PartyDecoyEvent = EIslandDecoyEvent::None;
    Round->PartyOpeningTeam = INDEX_NONE;
    Round->PartySite = INDEX_NONE;
    Round->PartyRewardMask = 0;
    PartyOpenerTeam = OpeningTeam;
    CurrentPartyFeature = PartyFeatureForChest(ChestIndex);
    SetPhase(EIslandPhase::PartyLoading);
    ++PartyRoundId;
    PartyChestIndex = ChestIndex;
    PartyDeadline = FPlatformTime::Seconds() + PartyAssetTimeout;
    bPartyPrepareFailed = false;
    bPartyAwaitingBlack = false;
    bPartyRestored = false;
    bPartyRevealSent = false;
    bPartyCompleted = false;

    for (int32 Seat = 0; Seat < GetPlayers().Num(); ++Seat)
    {
        auto* Player = CastChecked<AIslandPlayerController>(GetPlayers()[Seat]);
        FPartyPlayer& Entry = PartyPlayers.Add(Player);
        Entry.Seat = Seat;
        Entry.Original = Player->GetHero();
        if (!Entry.Original.IsValid()) bPartyPrepareFailed = true;
        if (!bExpeditionRoom)
        {
            PartyScreenBlack(*Player, PartyRoundId, false);
        }
    }
    UE_LOG(LogTemp, Display, TEXT("PARTY_PREPARE round=%u chest=%d players=%d"), PartyRoundId, ChestIndex, PartyPlayers.Num());
    for (const auto& Pair : PartyPlayers)
    {
        Pair.Key->ClientPrepareParty(PartyRoundId, CurrentPartyFeature, bExpeditionRoom);
    }
}

AIslandGameMode::FPartyPlayer* AIslandGameMode::FindPartyPlayer(AIslandPlayerController& Player, uint32 RoundId)
{
    return RoundId == PartyRoundId ? PartyPlayers.Find(&Player) : nullptr;
}

bool AIslandGameMode::CanJumpBeforePartyBlack(const AIslandPlayerController& Player) const
{
    const auto* Hero = Player.GetHero();
    return bExpeditionRoom && GetGameState<AIslandGameState>()->Phase == EIslandPhase::PartyLoading
        && Super::CanPlayerAct(Player) && Hero && !Hero->IsMudStunned()
        && Hero->GetCharacterMovement()->MovementMode != MOVE_None;
}

void AIslandGameMode::PartyAssetsReady(AIslandPlayerController& Player, uint32 RoundId, bool bSuccess)
{
    if (GetGameState<AIslandGameState>()->Phase == EIslandPhase::PartyLoading)
    {
        if (FPartyPlayer* Entry = FindPartyPlayer(Player, RoundId); Entry && !Entry->bAssetsReady)
        {
            Entry->bAssetsReady = bSuccess;
            bPartyPrepareFailed |= !bSuccess;
        }
    }
}

void AIslandGameMode::PartyScreenBlack(AIslandPlayerController& Player, uint32 RoundId, bool bReturning)
{
    const auto Expected = bReturning ? EIslandPhase::PartyReturning : EIslandPhase::PartyLoading;
    if (GetGameState<AIslandGameState>()->Phase != Expected) return;
    FPartyPlayer* Entry = FindPartyPlayer(Player, RoundId);
    if (!Entry) return;
    bool& bAcknowledged = bReturning ? Entry->bReturnBlack : Entry->bEntryBlack;
    if (bAcknowledged) return;
    bAcknowledged = true;
    if (!bReturning || (!Entry->bEntryBlack && Player.GetPawn() == Entry->Original.Get()))
    {
        Entry->View = Player.GetControlRotation();
        if (auto* Hero = Entry->Original.Get())
        {
            const FVector Feet = Hero->GetActorLocation();
            const FSeaState* Sea = SeaStates.Find(&Player);
            Entry->bReturnToDock = (Sea && Sea->RespawnAt > 0.) || Feet.Z < -970.
                || GetTerrain()->IsInSea(Feet + FVector(0, 0, 80.));
            Hero->SetWantsClimb(false);
        }
    }
    if (auto* Character = Cast<ACharacter>(Player.GetPawn()))
    {
        Character->StopJumping();
        Character->GetCharacterMovement()->StopMovementImmediately();
        Character->GetCharacterMovement()->DisableMovement();
    }
    Player.SetVoxelInputEnabled(false);
    UE_LOG(LogTemp, Display, TEXT("PARTY_VEIL_ACK round=%u direction=%s seat=%d"), RoundId,
        bReturning ? TEXT("out") : TEXT("in"), Entry->Seat);
}

void AIslandGameMode::PartyScreenClear(AIslandPlayerController& Player, uint32 RoundId, bool bReturning)
{
    const auto Expected = bReturning ? EIslandPhase::PartyReturning : EIslandPhase::PartyLoading;
    if (bPartyRevealSent && GetGameState<AIslandGameState>()->Phase == Expected)
        if (auto* Entry = FindPartyPlayer(Player, RoundId)) Entry->bArrivalVisible = true;
}

void AIslandGameMode::PartyPawnReady(AIslandPlayerController& Player, uint32 RoundId)
{
    if (GetGameState<AIslandGameState>()->Phase == EIslandPhase::PartyLoading)
    {
        if (FPartyPlayer* Entry = FindPartyPlayer(Player, RoundId); Entry && Entry->Temporary.IsValid()
            && Entry->Temporary.Get() == Player.GetPawn())
        {
            Entry->bPawnReady = true;
        }
    }
}

void AIslandGameMode::PartyUnloaded(AIslandPlayerController& Player, uint32 RoundId, bool bSuccess)
{
    if (GetGameState<AIslandGameState>()->Phase == EIslandPhase::PartyReturning)
    {
        if (FPartyPlayer* Entry = FindPartyPlayer(Player, RoundId); Entry && !Entry->bUnloaded)
        {
            Entry->bUnloaded = true;
            if (!bSuccess)
            {
                UnavailablePartyFeatures.Add(CurrentPartyFeature);
                bPartyUnavailable |= !bExpeditionRoom;
            }
        }
    }
}

void AIslandGameMode::UpdateParty()
{
    const EIslandPhase Phase = GetGameState<AIslandGameState>()->Phase;
    const bool bTimedOut = FPlatformTime::Seconds() >= PartyDeadline;
    if (Phase == EIslandPhase::PartyReturning)
    {
        if (!bPartyRestored)
        {
            const bool bAllBlack = Algo::AllOf(PartyPlayers, [](const auto& Pair) { return Pair.Value.bReturnBlack; });
            if (!bAllBlack)
            {
                if (bTimedOut && GameSession)
                {
                    // A suspended connection cannot present a blackout. Remove that participant
                    // through the ordinary disconnect path instead of exposing a pawn swap.
                    TArray<TWeakObjectPtr<AIslandPlayerController>> Stalled;
                    for (const auto& Pair : PartyPlayers)
                        if (!Pair.Value.bReturnBlack && Pair.Key.IsValid() && !Pair.Key->IsLocalController()) Stalled.Add(Pair.Key);
                    for (const auto& Player : Stalled)
                        if (Player.IsValid()) GameSession->KickPlayer(Player.Get(), FText::FromString(TEXT("小游戏返岛连接超时")));
                    PartyDeadline = FPlatformTime::Seconds() + 5.;
                }
                return;
            }
            RestorePartyPlayers();
            return;
        }
        const bool bAllUnloaded = Algo::AllOf(PartyPlayers, [](const auto& Pair) { return Pair.Value.bUnloaded; });
        if (bAllUnloaded || bTimedOut)
        {
            if (!bAllUnloaded)
            {
                UnavailablePartyFeatures.Add(CurrentPartyFeature);
                bPartyUnavailable |= !bExpeditionRoom;
            }
            if (bExpeditionRoom && !bPartyRevealSent)
            {
                bPartyRevealSent = true;
                PartyDeadline = FPlatformTime::Seconds() + PartyDisplayTimeout;
                for (const auto& Pair : PartyPlayers)
                    if (auto* Player = Pair.Key.Get())
                    {
                        if (auto* Hero = Player->GetHero()) Hero->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
                        Player->ClientRevealParty(PartyRoundId, true);
                    }
                return;
            }
            if (bExpeditionRoom && !bTimedOut
                && !Algo::AllOf(PartyPlayers, [](const auto& Pair) { return Pair.Value.bArrivalVisible; })) return;
            for (const auto& Pair : PartyPlayers)
                if (auto* Player = Pair.Key.Get(); Player && !Player->IsLocalController()) Player->SetVoxelInputEnabled(true);
            auto* Round = GetGameState<AIslandGameState>();
            const double ResumeTime = Round->GetServerWorldTimeSeconds();
            if (Round->PartyOutcome != EIslandChestOutcome::Unknown)
            {
                Round->PartyWinnerUntil = ResumeTime + 8.;
                if (Round->PartyOutcome == EIslandChestOutcome::Decoy) Round->BeginDecoyEffect();
            }
            for (auto& Pair : ChestLocks) Pair.Value.FinishAt += ResumeTime - Round->PartyStartedAt;
            PartyPlayers.Reset();
            PartyChestIndex = INDEX_NONE;
            SetPhase(EIslandPhase::Hunting);
            UE_LOG(LogTemp, Display, TEXT("PARTY_HUNTING round=%u unloaded=%d available=%d"), PartyRoundId, bAllUnloaded, !bPartyUnavailable);
        }
        return;
    }
    if (Phase != EIslandPhase::PartyLoading)
    {
        return;
    }
    if (bPartyPrepareFailed)
    {
        UE_LOG(LogTemp, Display, TEXT("PARTY_ABORT round=%u timeout=0 gate=prepare"), PartyRoundId);
        ReturnFromParty();
        return;
    }
    const auto WaitFor = [this](const TCHAR* Gate)
    {
        if (FPlatformTime::Seconds() < PartyDeadline) return;
        UE_LOG(LogTemp, Display, TEXT("PARTY_ABORT round=%u timeout=1 gate=%s"), PartyRoundId, Gate);
        ReturnFromParty();
    };

    if (!PartySession)
    {
        if (!Algo::AllOf(PartyPlayers, [](const auto& Pair) { return Pair.Value.bAssetsReady; }))
        {
            WaitFor(TEXT("assets"));
            return;
        }
        // Loading can complete long before a busy or occluded viewport presents
        // solid black. Give the renderer its own deadline once every asset is ready.
        if (!bPartyAwaitingBlack)
        {
            bPartyAwaitingBlack = true;
            PartyDeadline = FPlatformTime::Seconds() + PartyDisplayTimeout;
        }
        if (!Algo::AllOf(PartyPlayers, [](const auto& Pair) { return Pair.Value.bEntryBlack; }) || GetTerrain()->IsBusy())
        {
            WaitFor(TEXT("blackout-or-terrain"));
            return;
        }
        UClass* SessionClass = nullptr;
        for (const auto& Pair : PartyPlayers)
        {
            if (Pair.Key->IsLocalController())
            {
                SessionClass = Pair.Key->GetPartySessionClass();
                break;
            }
        }
        if (SessionClass)
        {
            const FTransform Transform(bExpeditionRoom ? APartyVoxelSession::ArenaOrigin() : FVector(8000, 0, 1700));
            PartySession = GetWorld()->SpawnActorDeferred<APartySession>(SessionClass, Transform, nullptr, nullptr,
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (PartySession)
            {
                if (auto* VoxelParty = Cast<APartyVoxelSession>(PartySession))
                    VoxelParty->SetArenaSeed(static_cast<int32>(HashCombineFast(uint32(PartyLayoutSeed), PartyRoundId)));
                for (const auto& Pair : PartyPlayers)
                    if (Pair.Key.IsValid()) PartySession->SetParticipantTeam(Pair.Value.Seat, Pair.Key->GetTeam());
                PartySession->FinishSpawning(Transform);
            }
        }
        if (!PartySession)
        {
            bPartyPrepareFailed = true;
            return;
        }
        PartySession->OnPartyFinished.BindUObject(this, &AIslandGameMode::ReturnFromParty);
        PartyDeadline = FPlatformTime::Seconds() + PartyAssetTimeout;
        UE_LOG(LogTemp, Display, TEXT("PARTY_SWAP round=%u direction=in players=%d"), PartyRoundId, PartyPlayers.Num());
        for (auto& Pair : PartyPlayers)
        {
            FPartyPlayer& Entry = Pair.Value;
            APartyPawn* Pawn = PartySession->CreateParticipant(Entry.Seat);
            if (!Pawn)
            {
                bPartyPrepareFailed = true;
                return;
            }
            Entry.Temporary = Pawn;
            Pair.Key->Possess(Pawn);
            bPartyPrepareFailed |= Pair.Key->GetPawn() != Pawn;
        }
        return;
    }

    if (Algo::AllOf(PartyPlayers, [](const auto& Pair) { return Pair.Value.bPawnReady; }))
    {
        if (bExpeditionRoom && !bPartyRevealSent)
        {
            bPartyRevealSent = true;
            PartyDeadline = FPlatformTime::Seconds() + PartyDisplayTimeout;
            for (const auto& Pair : PartyPlayers) Pair.Key->ClientRevealParty(PartyRoundId, false);
            return;
        }
        if (bExpeditionRoom && !Algo::AllOf(PartyPlayers, [](const auto& Pair) { return Pair.Value.bArrivalVisible; }))
        {
            WaitFor(TEXT("arrival"));
            return;
        }
        if (!bExpeditionRoom)
        {
            Exposed[PartyChestIndex]->Destroy();
            Exposed[PartyChestIndex] = nullptr;
            ConsumedDecoys.Add(PartyChestIndex);
        }
        GetGameState<AIslandGameState>()->ClearDecoyEffect();
        SetPhase(EIslandPhase::PartyPlaying);
        PartySession->StartParty();
        UE_LOG(LogTemp, Display, TEXT("PARTY_START round=%u chest=%d players=%d"), PartyRoundId, PartyChestIndex, PartyPlayers.Num());
    }
    else WaitFor(TEXT("pawn-ready"));
}

void AIslandGameMode::ReturnFromParty()
{
    auto* Round = GetGameState<AIslandGameState>();
    if (Round->Phase != EIslandPhase::PartyPlaying && Round->Phase != EIslandPhase::PartyLoading) return;
    const bool bFinishedGame = Round->Phase == EIslandPhase::PartyPlaying;
    if (bDecoySandPending) return;
    if (bFinishedGame && bExpeditionRoom && PartySession)
    {
        AwardPartyTreasure();
        return;
    }
    CompletePartyReturn(bFinishedGame);
}

void AIslandGameMode::CompletePartyReturn(bool bFinishedGame)
{
    auto* Round = GetGameState<AIslandGameState>();
    if (bFinishedGame && bExpeditionRoom)
    {
        Round->ForceNetUpdate();
        UE_LOG(LogTemp, Display, TEXT("PARTY_CHEST_SETTLED round=%u site=%d outcome=%d opener=%d winner=%d rewarded=%d winner_mask=%d"),
            PartyRoundId, PartyChestIndex, int32(Round->PartyOutcome), PartyOpenerTeam, Round->PartyWinnerTeam,
            Round->PartyRewardMask, Round->PartyWinnerMask);
    }
    SetPhase(EIslandPhase::PartyReturning);
    PartyDeadline = FPlatformTime::Seconds() + (bExpeditionRoom ? PartyDisplayTimeout : PartyAssetTimeout);
    bPartyCompleted = bFinishedGame;
    bPartyRevealSent = false;
    for (auto& Pair : PartyPlayers)
    {
        Pair.Value.bArrivalVisible = false;
        Pair.Value.bReturnBlack = !bExpeditionRoom;
        if (Pair.Key.IsValid()) Pair.Key->ClientBeginPartyReturn(PartyRoundId);
    }
    if (!bExpeditionRoom) RestorePartyPlayers();
}

void AIslandGameMode::RestorePartyPlayers()
{
    if (bPartyRestored) return;
    bPartyRestored = true;
    PartyDeadline = FPlatformTime::Seconds() + PartyAssetTimeout;
    auto* Round = GetGameState<AIslandGameState>();
    UE_LOG(LogTemp, Display, TEXT("PARTY_SWAP round=%u direction=out players=%d"), PartyRoundId, PartyPlayers.Num());
    FDecoyWorldPlan ReturnPlan;
    if (Round->PartyDecoyEvent == EIslandDecoyEvent::CrewRally) PlanRallyLandings(ReturnPlan);
    for (auto& Pair : PartyPlayers)
    {
        AIslandPlayerController* Controller = Pair.Key.Get();
        if (!Controller)
        {
            continue;
        }
        AIslandPlayerController& Player = *Controller;
        FPartyPlayer& Entry = Pair.Value;
        if (AVoxelCharacter* Hero = Entry.Original.Get())
        {
            if (Player.GetPawn() != Hero)
            {
                Player.Possess(Hero);
            }
            const bool bBlocked = IsPartyReturnBlocked(*Hero);
            const bool bWillScatter = Round->PartyDecoyEvent == EIslandDecoyEvent::Scatter && !Round->IsPartyWinner(Player.GetTeam());
            const FTransform* RallyTarget = ReturnPlan.RallyLandings.Find(Pair.Key);
            const auto Landing = RallyTarget
                ? FindStandingFloor(Hero, RallyTarget->GetLocation() + FVector(0,0,20.), 200.) : TOptional<FVector>{};
            if (Landing && FMath::Abs(Landing->Z - RallyTarget->GetLocation().Z) < 40.)
            {
                BurialSites.Remove(&Player);
                UExpeditionMudMarkEffect::Clear(*Hero);
                Hero->ReturnToStart(FTransform(RallyTarget->GetRotation(), Landing.GetValue()));
                Player.SetControlRotation(Entry.View);
                Player.ClientResetView(Entry.View);
            }
            else if (!bWillScatter && (Entry.bReturnToDock || bBlocked))
            {
                ReturnToSafeGround(Player);
            }
            else if (!bWillScatter)
            {
                Hero->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
                Player.SetControlRotation(Entry.View);
                Player.ClientResetView(Entry.View);
            }
        }
        else
        {
            Player.UnPossess();
            const FTransform Target = FindSafeReturn(Player);
            RestartPlayerAtTransform(&Player, Target);
            Player.ClientResetView(Target.Rotator());
        }
        if (APartyPawn* Pawn = Entry.Temporary.Get())
        {
            Pawn->Destroy();
        }
        SeaStates.Remove(&Player);
        Player.ClientWaterState(0);
        if (!bExpeditionRoom) Player.SetVoxelInputEnabled(true);
    }
    if (Round->PartyDecoyEvent == EIslandDecoyEvent::Scatter) ScatterLosingCrews();
    if (PartySession)
    {
        PartySession->Destroy();
        PartySession = nullptr;
    }
    for (const auto& Pair : PartyPlayers)
    {
        if (AIslandPlayerController* Player = Pair.Key.Get())
        {
            if (bExpeditionRoom && Player->GetHero()) Player->GetHero()->GetCharacterMovement()->DisableMovement();
            if (!bPartyCompleted)
            {
                Player->ClientEditRejected(TEXT("小游戏未能就绪，已回到寻宝岛"));
            }
            Player->ClientReturnFromParty(PartyRoundId);
        }
    }
    UE_LOG(LogTemp, Display, TEXT("PARTY_RETURN round=%u"), PartyRoundId);
}

void AIslandGameMode::RemovePartyPlayer(AIslandPlayerController& Player)
{
    FPartyPlayer Entry;
    if (!PartyPlayers.RemoveAndCopyValue(&Player, Entry))
    {
        return;
    }
    if (PartySession)
    {
        PartySession->RemoveParticipant(Entry.Seat);
    }
    if (APartyPawn* Pawn = Entry.Temporary.Get())
    {
        Pawn->Destroy();
    }
    if (AVoxelCharacter* Hero = Entry.Original.Get(); Hero && !Hero->GetController())
    {
        Hero->Destroy();
    }
}
