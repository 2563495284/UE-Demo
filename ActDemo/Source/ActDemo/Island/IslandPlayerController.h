#pragma once

#include "Island/IslandRules.h"
#include "Voxel/VoxelPlayerController.h"
#include "IslandPlayerController.generated.h"

UCLASS()
class AIslandPlayerController : public AVoxelPlayerController
{
    GENERATED_BODY()

public:
    int32 GetTeam() const;
    bool IsMapOpen() const { return bMapOpen; }
    const FIslandClues& GetLocalClues() const { return LocalClues; }
    FString WaterStatus() const;

    void Interact();
    void RestartIsland();

    UFUNCTION(Client, Reliable) void ClientUpdateClues(const FIslandClues& Snapshot);
    UFUNCTION(Client, Reliable) void ClientWaterState(uint8 State);

protected:
    void SetupInputComponent() override;

private:
    friend class AIslandProbePeer;
    void OpenMap();
    void CloseMap();
    UFUNCTION(Server, Reliable) void ServerInteract();
    UFUNCTION(Server, Reliable) void ServerRestartIsland();

    FIslandClues LocalClues;
    double WaterChangedAt = 0.0;
    uint8 WaterState = 0;
    bool bMapOpen = false;
};
