#pragma once

#include "Island/IslandRules.h"
#include "Island/IslandRoomSettings.h"
#include "Voxel/VoxelGameMode.h"
#include "IslandGameMode.generated.h"

class AIslandChest;
class APartyPawn;
class APartySession;
class AIslandPlayerController;

UCLASS(Config = Game)
class ACTDEMO_API AIslandGameMode : public AVoxelGameMode
{
    GENERATED_BODY()

public:
    AIslandGameMode();
    void TryOpen(AIslandPlayerController& Player);
    void RestartIsland();
    void ChooseTeam(AIslandPlayerController& Player, int32 Team);
    void SetRoomReady(AIslandPlayerController& Player, bool bReady);
    void StartExpedition(AIslandPlayerController& Player);
    void ConfigureRoom(AIslandPlayerController& Player, const FIslandRoomSettings& Settings);
    void PartyAssetsReady(AIslandPlayerController& Player, uint32 RoundId, bool bSuccess);
    void PartyPawnReady(AIslandPlayerController& Player, uint32 RoundId);
    void PartyUnloaded(AIslandPlayerController& Player, uint32 RoundId, bool bSuccess);
    void RecoverPlayer(AVoxelPlayerController& Player) override;
    bool CanPlayerAct(const AVoxelPlayerController& Player) const override;

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
    void OnTerrainCommitted(const FVoxelEdit& Edit) override;
    void UpdateEnvironment() override;
    bool CanFillAt(const FVoxelEdit& Edit) const override;

private:
    friend class AIslandProbe;
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
        bool bPawnReady = false;
        bool bUnloaded = false;
    };

    void BeginParty(int32 ChestIndex);
    void UpdateParty();
    void ReturnFromParty();
    void RemovePartyPlayer(AIslandPlayerController& Player);
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
    FTransform FindSafeReturn(const AVoxelPlayerController& Player);
    int32 FindReachableChest(const AIslandPlayerController& Player) const;
    void GiveClue(int32 Team, int32 Index);
    void PublishWinner(int32 Team);

    FIslandTreasureLayout HiddenLayout;
    TArray<FIslandClues> TeamClues;
    UPROPERTY() TArray<TObjectPtr<AIslandChest>> Exposed;
    TMap<const AVoxelPlayerController*, FSeaState> SeaStates;
    TSet<int32> ConsumedDecoys;

    UPROPERTY(Config) int32 DecoyChestCount = 4;
    UPROPERTY(Config) FName DecoyMinigameFeature = TEXT("PartyButtonChallenge");
    UPROPERTY() TObjectPtr<APartySession> PartySession;
    TMap<TWeakObjectPtr<AIslandPlayerController>, FPartyPlayer> PartyPlayers;
    uint32 PartyRoundId = 0;
    int32 PartyChestIndex = INDEX_NONE;
    double PartyDeadline = 0.0;
    bool bPartyPrepareFailed = false;
    bool bPartyUnavailable = false;
    bool bRestarting = false;
};
