#pragma once

#include "Island/IslandRules.h"
#include "Voxel/VoxelGameMode.h"
#include "IslandGameMode.generated.h"

class AIslandChest;
class AIslandPlayerController;

UCLASS()
class ACTDEMO_API AIslandGameMode : public AVoxelGameMode
{
    GENERATED_BODY()

public:
    AIslandGameMode();
    void TryOpen(AIslandPlayerController& Player);
    void RestartIsland();
    void RecoverPlayer(AVoxelPlayerController& Player) override;
    bool CanPlayerAct(const AVoxelPlayerController& Player) const override;

protected:
    void BeginPlay() override;
    void PostLogin(APlayerController* NewPlayer) override;
    void Logout(AController* Exiting) override;
    AActor* ChoosePlayerStart_Implementation(AController* Player) override;
    int32 RoomCapacity() const override { return IslandRules::Seats; }
    bool StopOnGuestExit() const override { return false; }
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

    AActor* FindDock(int32 Seat) const;
    int32 FindReachableChest(const AIslandPlayerController& Player) const;
    void GiveClue(int32 Team, int32 Index);
    void PublishWinner(int32 Team);

    FIslandTreasureLayout HiddenLayout;
    TStaticArray<FIslandClues, IslandRules::TeamCount> TeamClues;
    UPROPERTY() TArray<TObjectPtr<AIslandChest>> Exposed;
    TMap<const AVoxelPlayerController*, FSeaState> SeaStates;
    bool bRestarting = false;
};
