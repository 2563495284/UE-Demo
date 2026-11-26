#pragma once

#include "Island/IslandRules.h"
#include "Island/IslandRoomSettings.h"
#include "Island/ExpeditionMudRules.h"
#include "Voxel/VoxelGameMode.h"
#include "IslandGameMode.generated.h"

class AIslandChest;
class AIslandNeutralProp;
class APartyPawn;
class APartySession;
class AIslandPlayerController;
class AExpeditionMudBall;

UCLASS(Config = Game)
class ACTDEMO_API AIslandGameMode : public AVoxelGameMode
{
    GENERATED_BODY()

public:
    AIslandGameMode();
    void TryOpen(AIslandPlayerController& Player);
    void RallyCrew(AIslandPlayerController& Sender);
    void RestartIsland();
    void ChooseTeam(AIslandPlayerController& Player, int32 Team);
    void ChooseCharacter(AIslandPlayerController& Player, int32 ModelIndex);
    void SetRoomReady(AIslandPlayerController& Player, bool bReady);
    void StartExpedition(AIslandPlayerController& Player);
    void ConfigureRoom(AIslandPlayerController& Player, const FIslandRoomSettings& Settings);
    void PartyAssetsReady(AIslandPlayerController& Player, uint32 RoundId, bool bSuccess);
    void PartyScreenBlack(AIslandPlayerController& Player, uint32 RoundId, bool bReturning);
    void PartyScreenClear(AIslandPlayerController& Player, uint32 RoundId, bool bReturning);
    void PartyPawnReady(AIslandPlayerController& Player, uint32 RoundId);
    void PartyUnloaded(AIslandPlayerController& Player, uint32 RoundId, bool bSuccess);
    void RecoverPlayer(AVoxelPlayerController& Player) override;
    TOptional<FVector> FindStandingFloor(const AVoxelCharacter* Hero, FVector Start, double Reach) const;
    bool CanPlayerAct(const AVoxelPlayerController& Player) const override;
    bool CanJumpBeforePartyBlack(const AIslandPlayerController& Player) const;
    void QueueHopperSand(AIslandNeutralProp& Hopper, const TArray<FVector>& Centers);
    void OnPlayerJumped(AVoxelCharacter& Hero) override;
    void ThrowExpeditionMud(AIslandPlayerController& Player, const FVector& Aim);
    int32 GetMudMarks(const AVoxelCharacter& Target) const;

protected:
    void InitGame(const FString& MapName, const FString& Options, FString& Error) override;
    void InitGameState() override;
    FString InitNewPlayer(APlayerController* Player, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal) override;
    void BeginPlay() override;
    void Tick(float DeltaSeconds) override;
    void PostLogin(APlayerController* NewPlayer) override;
    void Logout(AController* Exiting) override;
    AActor* ChoosePlayerStart_Implementation(AController* Player) override;
    int32 RoomCapacity() const override { return bExpeditionRoom ? Settings.MaxPlayers : IslandRules::Seats; }
    bool IsStartRequested() const override;
    bool IsAdmissionLocked() const override;
    FString AdmissionError() const override;
    void RoundSetupFailed(const FString& Error) override;
    bool StopOnGuestExit() const override { return false; }
    bool PrepareRound(FString& Error) override;
    bool TryStrikeProp(AVoxelCharacter& Hero, const FVector& Aim) override;
    void OnRoundStarted() override;
    void OnTerrainCommitted(const FVoxelEdit& Edit, AVoxelPlayerController* Source) override;
    void UpdateEnvironment() override;
    bool CanFillAt(const FVoxelEdit& Edit) const override;
    void GetProtectedMudVolumes(TArray<FBox>& OutVolumes) const override;

private:
    friend class AIslandProbe;
    friend class AExpeditionMudBall;
    AVoxelCharacter* ResolveMudImpact(AIslandPlayerController& Thrower, const FHitResult& Hit, uint8& Marks);
    void AddMudShellLayer(AIslandPlayerController& Target);
    void CheckMudLandings();
    TMap<AIslandPlayerController*, FExpeditionBurialSite> BurialSites;
    UPROPERTY() TArray<TObjectPtr<AIslandNeutralProp>> NeutralProps;
    struct FSeaState
    {
        double ImmersedAt = 0.0;
        double RespawnAt = 0.0;
    };

    struct FPartyPlayer
    {
        TWeakObjectPtr<AVoxelCharacter> Original;
        TWeakObjectPtr<APartyPawn> Temporary;
        FRotator View = FRotator::ZeroRotator;
        int32 Seat = INDEX_NONE;
        bool bReturnToDock = false;
        bool bAssetsReady = false;
        bool bEntryBlack = false;
        bool bReturnBlack = false;
        bool bArrivalVisible = false;
        bool bPawnReady = false;
        bool bUnloaded = false;
    };

    void BeginParty(int32 ChestIndex, int32 OpeningTeam = INDEX_NONE);
    void UpdateParty();
    void ReturnFromParty();
    void RestorePartyPlayers();
    void RemovePartyPlayer(AIslandPlayerController& Player);
    void PrepareExpeditionParties(int32 Seed);
    FName PartyFeatureForChest(int32 Index) const;
    void AwardPartyTreasure(bool bAllowSand = true);
    void CompletePartyReturn(bool bFinishedGame);
    struct FPartyFinish
    {
        int32 Participants = 0;
        int32 CrewMask = 0;
        int32 WinnerMask = 0;
        int32 WinnerTeam = INDEX_NONE;
        FString WinnerName;
    };
    void PublishPartyFinish(const FPartyFinish& Finish);
    struct FDecoyWorldPlan
    {
        TMap<TWeakObjectPtr<AIslandPlayerController>, FTransform> RallyLandings;
        TArray<FVector> SandCenters;
        TArray<TWeakObjectPtr<AIslandNeutralProp>> Sails;
        int32 Mask() const;
    };
    FDecoyWorldPlan PlanDecoyWorldEvents() const;
    void PlanRallyLandings(FDecoyWorldPlan& Plan) const;
    bool IsPartyReturnBlocked(const AVoxelCharacter& Hero) const;
    TOptional<FTransform> FindRallyLanding(const FPartyPlayer& Entry, FVector Origin, TConstArrayView<FVector> Reserved) const;
    TArray<FVector> FindDecoySandRamp() const;
    void SettleDecoy(EIslandDecoyEvent Event, const FDecoyWorldPlan& Plan, const FPartyFinish& Finish);
    bool bDecoySandPending = false;
    void ScatterLosingCrews();
    void SetPhase(EIslandPhase Phase);
    FPartyPlayer* FindPartyPlayer(AIslandPlayerController& Player, uint32 RoundId);

    AActor* FindDock(int32 Seat) const;
    bool PositionCrews();
    void UpdateRoom();
    void CancelStart(const FString& Reason);
    int32 AvailableTeam(int32 Preferred, const AIslandPlayerController* Except) const;
    bool bExpeditionRoom = false;
    FIslandRoomSettings Settings;
    TMap<FGuid, int32> ReturningTeams;
    double ReturnDeadline = 0.0;
    void ReturnToSafeGround(AVoxelPlayerController& Player);
    FTransform FindSafeReturn(const AVoxelPlayerController& Player, TOptional<FVector> InlandTarget = {},
        TConstArrayView<FVector> KeepAwayFrom = {});
    int32 FindReachableChest(const AIslandPlayerController& Player) const;
    void RevealChestIfClear(int32 Index);
    bool GiveClue(int32 Team, int32 Index);
    void SendClueSnapshot(int32 Team);
    void PublishWinner(int32 Team);

    struct FChestLock
    {
        int32 Team = INDEX_NONE;
        TWeakObjectPtr<AIslandPlayerController> Leader;
        TWeakObjectPtr<AIslandPlayerController> Helper;
        double FinishAt = 0.0;
        bool bAssisted = false;
        bool bSeized = false;

        void SetHelper(AIslandPlayerController* NewHelper, double Now);
    };
    bool CanClaimChest(int32 Team, int32 Index) const;
    void WorkChest(AIslandPlayerController& Player, int32 Index);
    void UpdateChestLocks(double Now);
    void PublishLockViews();

    TMap<int32, FChestLock> ChestLocks;
    double NextLockCheck = 0.0;
    TMap<const AIslandPlayerController*, double> NextRallyAt;

    FIslandTreasureLayout HiddenLayout;
    TArray<FIslandClues> TeamClues;
    UPROPERTY() TArray<TObjectPtr<AIslandChest>> Exposed;
    TMap<const AVoxelPlayerController*, FSeaState> SeaStates;
    TSet<int32> ConsumedDecoys;

    UPROPERTY(Config) int32 DecoyChestCount = 4;
    UPROPERTY(Config) FName DecoyMinigameFeature = TEXT("PartyButtonChallenge");
    UPROPERTY() TObjectPtr<APartySession> PartySession;
    TMap<TWeakObjectPtr<AIslandPlayerController>, FPartyPlayer> PartyPlayers;
    TArray<FName> ExpeditionPartyDeck;
    TSet<FName> UnavailablePartyFeatures;
    FName CurrentPartyFeature;
    int32 PartyLayoutSeed = 0;
    int32 DecoyEventSeed = 0;
    uint32 PartyRoundId = 0;
    int32 PartyChestIndex = INDEX_NONE;
    int32 PartyOpenerTeam = INDEX_NONE;
    FRandomStream ScatterRandom;
    double PartyDeadline = 0.0;
    bool bPartyPrepareFailed = false;
    bool bPartyAwaitingBlack = false;
    bool bPartyRestored = false;
    bool bPartyRevealSent = false;
    bool bPartyCompleted = false;
    bool bPartyUnavailable = false;
    bool bRestarting = false;
};
