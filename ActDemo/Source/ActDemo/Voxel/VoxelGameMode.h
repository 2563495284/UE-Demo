#pragma once

#include "GameFramework/GameModeBase.h"
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
    static constexpr double AimReach = 235.0;

    void ReceiveReady(AVoxelPlayerController& Player, uint32 Checksum);
    void RequestEdit(AVoxelPlayerController& Player, EVoxelAction Action, const FVector& Aim);
    // Server-only. The impact uses the first blocking surface; burial uses a fixed foot anchor.
    void QueueMudLanding(const FVector& Impact, const FVector& Normal, TFunction<void(bool)> Completion = {});
    void QueueBurialLayer(AVoxelCharacter& Victim, const FVector& FootAnchor, int32 Layer, TFunction<void(bool)> Completion = {});
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
    virtual bool TryStrikeProp(AVoxelCharacter& Hero, const FVector& Aim);
    virtual void OnRoundStarted() {}
    virtual void OnTerrainCommitted(const FVoxelEdit& Edit, AVoxelPlayerController* Source) {}
    virtual void UpdateEnvironment();
    virtual void GetProtectedMudVolumes(TArray<FBox>& OutVolumes) const {}
    virtual bool CanFillAt(const FVoxelEdit& Edit) const { return true; }
    const TArray<TObjectPtr<AVoxelPlayerController>>& GetPlayers() const { return Players; }
    void FinishRound(const FString& Message);
    void EnqueueEnvironmentFill(const TArray<FVector>& Centers, TFunction<void(bool)> Completion);

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
    };

    void EnqueueMud(FVoxelEdit Edit, TFunction<void(bool)> Completion);
    bool CheckMudPlacement(const FVoxelEdit& Edit, TMap<AVoxelCharacter*, FVector>* Lifts = nullptr) const;
    void EnqueueEdit(AVoxelPlayerController& Player, EVoxelAction Action, const FVector& Aim, TOptional<FIntVector> ProbeCenter);
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
