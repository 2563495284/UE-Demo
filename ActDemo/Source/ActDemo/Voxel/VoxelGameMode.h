#pragma once

#include "GameFramework/GameModeBase.h"
#include "PartyPunch.h"
#include "Voxel/VoxelDensity.h"
#include "VoxelGameMode.generated.h"

class AVoxelCharacter;
class AVoxelPlayerController;
class AVoxelTerrain;

UCLASS()
class ACTDEMO_API AVoxelGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AVoxelGameMode();
    static constexpr double AimReach = PartyPunch::Reach;

    void ReceiveReady(AVoxelPlayerController& Player, uint32 Checksum);
    void RequestEdit(AVoxelPlayerController& Player, EVoxelAction Action, const FVector& Aim, uint8 Power=0, bool bProtectChest=false);
    // Server-only, replayed as a single authoritative terrain edit.
    void QueueMudLanding(const FVector& Impact, const FVector& Normal, bool bWideFootprint, TFunction<void(bool)> Completion = {});
    virtual void RecoverPlayer(AVoxelPlayerController& Player);
    AVoxelTerrain* GetTerrain() const { return Terrain; }
    bool IsPlaying() const { return bPlaying; }
    virtual bool CanPlayerAct(const AVoxelPlayerController& Player) const;
    virtual void OnPlayerJumped(AVoxelCharacter& Hero) {}

protected:
    void BeginPlay() override;
    void Tick(float DeltaSeconds) override;
    void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options,
        const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    void PostLogin(APlayerController* NewPlayer) override;
    void Logout(AController* Exiting) override;
    AActor* ChoosePlayerStart_Implementation(AController* Player) override;

    virtual int32 RoomCapacity() const { return 2; }
    virtual bool IsStartRequested() const { return Players.Num() == RoomCapacity(); }
    virtual bool IsAdmissionLocked() const { return false; }
    virtual FString AdmissionError() const { return TEXT("The voxel room is full or already started."); }
    virtual void RoundSetupFailed(const FString& Error) {}
    virtual bool StopOnGuestExit() const { return true; }
    virtual bool PrepareRound(FString& Error) { return true; }
    virtual bool TryStrikeProp(AVoxelCharacter& Hero, const FVector& Aim, uint8 PunchPower, bool bProtectChest);
    virtual void OnRoundStarted() {}
    virtual void OnTerrainCommitted(const FVoxelEdit& Edit, AVoxelPlayerController* Source) {}
    virtual void UpdateEnvironment();
    virtual void GetProtectedMudVolumes(TArray<FBox>& OutVolumes) const {}
    virtual bool CanFillAt(const FVoxelEdit& Edit) const { return true; }
    const TArray<TObjectPtr<AVoxelPlayerController>>& GetPlayers() const { return Players; }
    void FinishRound(const FString& Message);
    void EnqueueEnvironmentFill(const TArray<FVector>& Centers, TFunction<void(bool)> Completion);
    bool CanStartEnvironmentFill(const TArray<FVector>& Centers) const;
    void ClipProtectedMud(FVoxelEdit& Edit) const;
    bool CheckMudOccupants(const FVoxelEdit& Edit, TMap<AVoxelCharacter*, FVector>* Lifts,
        TArray<FVoxelMudClearance>* AppendClearances = nullptr) const;
    static void LiftMudOccupants(const TMap<AVoxelCharacter*, FVector>& Lifts);

private:
    friend class AVoxelPlaygroundProbe;
    friend class AIslandProbe;
    friend class AExpeditionWalk;
    struct FRequest
    {
        TWeakObjectPtr<AVoxelPlayerController> Player;
        EVoxelAction Action;
        FVector Origin;
        FVector Aim;
        double ReceivedAt;
        TOptional<FIntVector> ProbeCenter;
        TOptional<FVoxelEdit> EnvironmentFill;
        TFunction<void(bool)> Completion;
        uint8 PunchPower = 0;
        bool bProtectChest = false;
    };

    void EnqueueMud(FVoxelEdit Edit, TFunction<void(bool)> Completion);
    bool MakeEnvironmentFill(const TArray<FVector>& Centers, FVoxelEdit& Fill) const;
    bool CheckMudPlacement(const FVoxelEdit& Edit, TMap<AVoxelCharacter*, FVector>* Lifts = nullptr) const;
    void EnqueueEdit(AVoxelPlayerController& Player, EVoxelAction Action, const FVector& Aim, TOptional<FIntVector> ProbeCenter, uint8 Power=0, bool bProtectChest=false);
    bool ResolveEdit(const FRequest& Request, AVoxelCharacter& Hero, FVoxelEdit& Edit) const;
    void StartNextEdit();
    void StartFrontRequest();
    void CancelPendingEdits();
    void TryStartRound();
    bool CanPlaceAgainstPlayers(const FVoxelEdit& Edit) const;
    bool IsFillAreaFree(const FVoxelEdit& Edit) const;
    bool RoomFull() const;

    UPROPERTY() TObjectPtr<AVoxelTerrain> Terrain;
    UPROPERTY() TArray<TObjectPtr<AVoxelPlayerController>> Players;
    TArray<FRequest> Requests;
    TWeakObjectPtr<AVoxelPlayerController> WorkingPlayer;
    bool bPumpingEdits = false;
    bool bPlaying = false;
    bool bLocked = false;
    bool bRoundSetupFailed = false;
};
